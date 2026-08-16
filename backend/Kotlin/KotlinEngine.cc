#include "KotlinEngine.h"

#include <ScriptX/ScriptX.h>

#include <filesystem>
#include <sstream>
#include <cstdint>

namespace script::kotlin_backend {

Arguments KotlinInterop::makeArguments(KotlinEngine* engine, KotlinValuePtr thiz,
                                       std::vector<KotlinValuePtr> args) {
  return Arguments(ArgumentsData{engine, std::move(thiz), std::move(args)});
}

KotlinEngine::KotlinEngine(std::shared_ptr<utils::MessageQueue> queue)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  host_ = KotlinRuntime::instance().createHost();
}

KotlinEngine::~KotlinEngine() = default;

void KotlinEngine::destroy() noexcept {
  if (destroying_) return;
  destroying_ = true;
  destroyUserData();
  releaseNativeFunctions(this);
  if (queue_) {
    queue_->removeMessageByTag(this);
    queue_->shutdown();
  }
  KotlinRuntime::instance().destroyHost(host_);
  host_ = nullptr;
  delete this;
}

bool KotlinEngine::isDestroying() const { return destroying_; }

Local<Value> KotlinEngine::get(const Local<String>& key) {
  auto object = KotlinRuntime::instance().get(host_, key.toString());
  auto result = wrapValue(object);
  // The runtime returns a local JNI reference. wrapValue retained it globally.
  return KotlinInterop::toLocal<Value>(std::move(result));
}

void KotlinEngine::set(const Local<String>& key, const Local<Value>& value) {
  auto object = value.isNull() ? nullptr : KotlinInterop::value(value)->object;
  KotlinRuntime::instance().set(host_, key.toString(), object);
}

Local<Value> KotlinEngine::eval(const Local<String>& script) {
  return eval(script, String::newString(""));
}

Local<Value> KotlinEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  if (destroying_) throw Exception("Kotlin engine is being destroyed");
  auto object = KotlinRuntime::instance().eval(host_, script.toString(), sourceFile.toString());
  return KotlinInterop::toLocal<Value>(wrapValue(object));
}

Local<Value> KotlinEngine::loadFile(const Local<String>& scriptFile) {
  const std::filesystem::path path(scriptFile.toString());
  if (path.empty() || !std::filesystem::is_regular_file(path)) {
    throw Exception("Kotlin JAR file not found: " + path.string());
  }
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (extension != ".jar") {
    throw Exception("Kotlin backend is JAR-only; loadFile accepts a compiled .jar, not " +
                    path.extension().string());
  }
  return KotlinInterop::toLocal<Value>(
      wrapValue(KotlinRuntime::instance().loadJar(host_, path.string())));
}

std::shared_ptr<utils::MessageQueue> KotlinEngine::messageQueue() { return queue_; }

void KotlinEngine::gc() {
  if (host_) KotlinRuntime::instance().gc(host_);
}

size_t KotlinEngine::getHeapSize() {
  const auto jvmBytes = host_ ? KotlinRuntime::instance().heapSize(host_) : 0;
  const auto associated = associatedMemory_.load();
  if (associated <= 0) return jvmBytes;
  return jvmBytes + static_cast<size_t>(associated);
}

void KotlinEngine::adjustAssociatedMemory(int64_t count) { associatedMemory_ += count; }

ScriptLanguage KotlinEngine::getLanguageType() { return ScriptLanguage::kKotlin; }

std::string KotlinEngine::getEngineVersion() { return KotlinRuntime::instance().version(); }

int64_t KotlinEngine::loadCompiledPlugin(const std::string& jarPath, const std::string& mainClass,
                                         const std::string& pluginName) {
  if (destroying_) throw Exception("Kotlin engine is being destroyed");
  return KotlinRuntime::instance().loadCompiledPlugin(host_, jarPath, mainClass, pluginName);
}

void KotlinEngine::enableCompiledPlugin(int64_t pluginHandle) {
  if (destroying_ || !host_ || !pluginHandle) return;
  KotlinRuntime::instance().enableCompiledPlugin(host_, pluginHandle);
}

void KotlinEngine::unloadCompiledPlugin(int64_t pluginHandle) {
  if (!host_ || !pluginHandle) return;
  KotlinRuntime::instance().unloadCompiledPlugin(host_, pluginHandle);
}

void KotlinEngine::performRegisterNativeClass(internal::TypeIndex typeIndex,
                                              const internal::ClassDefineState* classDefine,
                                              ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  static_cast<void>(typeIndex);
  static_cast<void>(instanceTypeToScriptClass);
  const auto classId = static_cast<int64_t>(reinterpret_cast<uintptr_t>(classDefine));
  const auto prefix = "__scriptx_native_" + std::to_string(reinterpret_cast<uintptr_t>(classDefine));
  const auto apiPrefix = "__scriptx_api_" + classDefine->className;
  KotlinRuntime::instance().registerNativeClass(host_, classId, classDefine->className);
  std::ostringstream prelude;

  auto instancePrelude = [&]() {
    std::ostringstream out;
    out << "class " << prefix
        << "Instance(private val __native: ScriptXKotlinHost.NativeInstance) {\n";
    out << "fun __scriptxNativeInstance(): ScriptXKotlinHost.NativeInstance = __native\n";
    for (const auto& function : classDefine->instanceDefine.functions) {
      out << "fun " << function.name << "(vararg args: Any?): Any? = __scriptxHost.call(\""
          << prefix << "_i_" << function.name << "\", __native.pointer(), __native, *args)\n";
    }
    for (const auto& property : classDefine->instanceDefine.properties) {
      out << (property.getter && !property.setter ? "val " : "var ") << property.name << ": Any?";
      if (!property.getter) out << " = null";
      out << "\n";
      if (property.getter)
        out << " get() = __scriptxHost.call(\"" << prefix << "_i_" << property.name
            << "_get\", __native.pointer(), __native)\n";
      if (property.setter)
        out << " set(value) { __scriptxHost.call(\"" << prefix << "_i_" << property.name
            << "_set\", __native.pointer(), __native, value) }\n";
    }
    out << "}\n";
    return out.str();
  };

  if (classDefine->instanceDefine.constructor) {
    auto constructor = createNativeConstructor(
        this, classId, classDefine->instanceDefine.constructor);
    auto wrapped = wrapValue(constructor, ValueKind::kFunction);
    KotlinRuntime::instance().setNative(host_, prefix + "_ctor", wrapped->object);
    KotlinRuntime::instance().setNative(host_, apiPrefix + "_ctor", wrapped->object);
    prelude << instancePrelude();
  }

  for (const auto& function : classDefine->staticDefine.functions) {
    auto callback = createNativeFunction(this, function.callback);
    auto wrapped = wrapValue(callback, ValueKind::kFunction);
    KotlinRuntime::instance().setNative(host_, prefix + "_s_" + function.name, wrapped->object);
    KotlinRuntime::instance().setNative(
        host_, apiPrefix + "_s_" + function.name, wrapped->object);
  }
  for (const auto& property : classDefine->staticDefine.properties) {
    if (property.getter) {
      auto callback = createNativeFunction(this, [getter = property.getter](const Arguments&) {
        return getter();
      });
      auto wrapped = wrapValue(callback, ValueKind::kFunction);
      KotlinRuntime::instance().setNative(host_, prefix + "_s_" + property.name + "_get",
                                          wrapped->object);
      KotlinRuntime::instance().setNative(
          host_, apiPrefix + "_s_" + property.name + "_get", wrapped->object);
    }
    if (property.setter) {
      auto callback = createNativeFunction(this, [setter = property.setter](const Arguments& args) {
        setter(args.size() ? args[0] : Local<Value>());
        return Local<Value>();
      });
      auto wrapped = wrapValue(callback, ValueKind::kFunction);
      KotlinRuntime::instance().setNative(host_, prefix + "_s_" + property.name + "_set",
                                          wrapped->object);
      KotlinRuntime::instance().setNative(
          host_, apiPrefix + "_s_" + property.name + "_set", wrapped->object);
    }
  }

  for (const auto& function : classDefine->instanceDefine.functions) {
    auto callback = createNativeFunction(this, [function = function.callback](const Arguments& args) {
      if (args.size() < 2 || !args[0].isNumber()) throw Exception("invalid Kotlin native receiver");
      auto pointer = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0].asNumber().toInt64()));
      std::vector<KotlinValuePtr> values;
      for (size_t i = 2; i < args.size(); ++i) values.push_back(KotlinInterop::value(args[i]));
      auto forwarded = KotlinInterop::makeArguments(
          args.engineAs<KotlinEngine>(), KotlinInterop::value(args[1]), std::move(values));
      return function(pointer, forwarded);
    });
    auto wrapped = wrapValue(callback, ValueKind::kFunction);
    KotlinRuntime::instance().setNative(host_, prefix + "_i_" + function.name, wrapped->object);
    KotlinRuntime::instance().setNative(
        host_, apiPrefix + "_i_" + function.name, wrapped->object);
  }
  for (const auto& property : classDefine->instanceDefine.properties) {
    if (property.getter) {
      auto callback = createNativeFunction(this, [getter = property.getter](const Arguments& args) {
        if (args.size() < 2 || !args[0].isNumber()) throw Exception("invalid Kotlin native receiver");
        auto pointer = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0].asNumber().toInt64()));
        return getter(pointer);
      });
      auto wrapped = wrapValue(callback, ValueKind::kFunction);
      KotlinRuntime::instance().setNative(host_, prefix + "_i_" + property.name + "_get",
                                          wrapped->object);
      KotlinRuntime::instance().setNative(
          host_, apiPrefix + "_i_" + property.name + "_get", wrapped->object);
    }
    if (property.setter) {
      auto callback = createNativeFunction(this, [setter = property.setter](const Arguments& args) {
        if (args.size() < 3 || !args[0].isNumber()) throw Exception("invalid Kotlin native receiver");
        auto pointer = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0].asNumber().toInt64()));
        setter(pointer, args[2]);
        return Local<Value>();
      });
      auto wrapped = wrapValue(callback, ValueKind::kFunction);
      KotlinRuntime::instance().setNative(host_, prefix + "_i_" + property.name + "_set",
                                          wrapped->object);
      KotlinRuntime::instance().setNative(
          host_, apiPrefix + "_i_" + property.name + "_set", wrapped->object);
    }
  }

  std::ostringstream object;
  object << "class " << prefix << "Class {\n";
  if (classDefine->instanceDefine.constructor) {
    object << "operator fun invoke(vararg args: Any?): " << prefix
           << "Instance = " << prefix << "Instance(__scriptxHost.call(\"" << prefix
           << "_ctor\", *args) as ScriptXKotlinHost.NativeInstance)\n";
  }
  for (const auto& function : classDefine->staticDefine.functions)
    object << "fun " << function.name
           << "(vararg args: Any?): Any? = __scriptxHost.call(\"" << prefix << "_s_"
           << function.name << "\", *args)\n";
  for (const auto& property : classDefine->staticDefine.properties) {
    object << (property.getter && !property.setter ? "val " : "var ") << property.name << ": Any?";
    if (!property.getter) object << " = null";
    object << "\n";
    if (property.getter)
      object << " get() = __scriptxHost.call(\"" << prefix << "_s_" << property.name
             << "_get\")\n";
    if (property.setter)
      object << " set(value) { __scriptxHost.call(\"" << prefix << "_s_" << property.name
             << "_set\", value) }\n";
  }
  object << "}\n";

  std::string declaration = object.str();
  prelude << declaration;
  if (classDefine->nameSpace.empty()) {
    prelude << "val " << classDefine->className << " = " << prefix << "Class()\n";
  } else {
    std::vector<std::string> parts;
    std::stringstream namespaces(classDefine->nameSpace);
    std::string part;
    while (std::getline(namespaces, part, '.')) parts.push_back(part);
    if (parts.empty()) {
      prelude << "val " << classDefine->className << " = " << prefix << "Class()\n";
    } else {
      for (size_t offset = parts.size(); offset > 0; --offset) {
        const auto index = offset - 1;
        prelude << "class " << prefix << "Namespace" << index << " { ";
        if (index + 1 == parts.size())
          prelude << "val " << classDefine->className << " = " << prefix << "Class()";
        else
          prelude << "val " << parts[index + 1] << " = " << prefix << "Namespace"
                  << (index + 1) << "()";
        prelude << " }\n";
      }
      prelude << "val " << parts.front() << " = " << prefix << "Namespace0()\n";
    }
  }
  KotlinRuntime::instance().addPrelude(host_, prelude.str());
}

Local<Object> KotlinEngine::performNewNativeClass(internal::TypeIndex typeIndex,
                                                   const internal::ClassDefineState* classDefine,
                                                   size_t size,
                                                   const Local<Value>* args) {
  static_cast<void>(typeIndex);
  if (!classDefine->instanceDefine.constructor)
    throw Exception("class " + classDefine->className + " has no constructor");
  std::vector<KotlinValuePtr> values;
  for (size_t i = 0; i < size; ++i) values.push_back(KotlinInterop::value(args[i]));
  const auto classId = static_cast<int64_t>(reinterpret_cast<uintptr_t>(classDefine));
  auto receiver = wrapValue(KotlinRuntime::instance().newNativeInstance(host_, 0, classId),
                            ValueKind::kObject);
  auto callbackArgs = KotlinInterop::makeArguments(this, receiver, std::move(values));
  void* pointer = classDefine->instanceDefine.constructor(callbackArgs);
  if (!pointer) throw Exception("can't create class " + classDefine->className);
  KotlinRuntime::instance().setNativeInstancePointer(receiver->object,
                                                     static_cast<int64_t>(reinterpret_cast<uintptr_t>(pointer)));
  return KotlinInterop::toLocal<Object>(std::move(receiver));
}

Local<Object> KotlinEngine::newNativeInstanceForExisting(
    void* pointer, const internal::ClassDefineState* classDefine) {
  const auto classId = static_cast<int64_t>(reinterpret_cast<uintptr_t>(classDefine));
  auto object = KotlinRuntime::instance().newNativeInstance(
      host_, static_cast<int64_t>(reinterpret_cast<uintptr_t>(pointer)), classId);
  return KotlinInterop::toLocal<Object>(wrapValue(object, ValueKind::kObject));
}

bool KotlinEngine::performIsInstanceOf(const Local<Value>& value,
                                       const internal::ClassDefineState* classDefine) {
  if (!value.isObject() || !value.val_) return false;
  return KotlinRuntime::instance().nativeInstanceClassId(KotlinInterop::value(value)->object) ==
         static_cast<int64_t>(reinterpret_cast<uintptr_t>(classDefine));
}

void* KotlinEngine::performGetNativeInstance(const Local<Value>& value,
                                             const internal::ClassDefineState* classDefine) {
  if (!performIsInstanceOf(value, classDefine)) return nullptr;
  return reinterpret_cast<void*>(static_cast<uintptr_t>(KotlinRuntime::instance().nativeInstancePointer(
      KotlinInterop::value(value)->object)));
}

}  // namespace script::kotlin_backend
