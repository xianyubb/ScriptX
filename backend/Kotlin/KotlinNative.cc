#include <ScriptX/ScriptX.h>

#include "KotlinEngine.h"

namespace script {

Arguments::Arguments(InternalCallbackInfoType callbackInfo)
    : callbackInfo_(std::move(callbackInfo)) {}
Arguments::~Arguments() = default;

Local<Object> Arguments::thiz() const {
  if (!hasThiz()) throw Exception("Kotlin function has no receiver");
  return kotlin_backend::KotlinInterop::toLocal<Object>(callbackInfo_.thiz);
}
bool Arguments::hasThiz() const { return callbackInfo_.thiz != nullptr; }
size_t Arguments::size() const { return callbackInfo_.args.size(); }
Local<Value> Arguments::operator[](size_t index) const {
  if (index >= callbackInfo_.args.size()) return {};
  return kotlin_backend::KotlinInterop::toLocal<Value>(callbackInfo_.args[index]);
}
ScriptEngine* Arguments::engine() const { return callbackInfo_.engine; }

void ScriptClass::performConstructFromCpp(internal::TypeIndex typeIndex,
                                          const internal::ClassDefineState* classDefine) {
  static_cast<void>(typeIndex);
  auto* engine = EngineScope::currentEngineAs<kotlin_backend::KotlinEngine>();
  if (!engine) throw Exception("constructing a Kotlin native class requires an EngineScope");
  auto object = engine->newNativeInstanceForExisting(this, classDefine);
  internalState_.scriptEngine = engine;
  internalState_.weakRef = Weak<Object>(object);
}

ScriptClass::ScriptClass(const Local<Object>& scriptObject) : internalState_{} {
  internalState_.scriptEngine = EngineScope::currentEngine();
  internalState_.weakRef = Weak<Object>(scriptObject);
}

Local<Object> ScriptClass::getScriptObject() const { return internalState_.weakRef.get(); }
Local<Array> ScriptClass::getInternalStore() const {
  auto object = getScriptObject();
  auto store = object.get("__scriptx_internal_store");
  if (store.isNull()) {
    auto created = Array::newArray();
    object.set("__scriptx_internal_store", created);
    return created;
  }
  return store.asArray();
}
ScriptEngine* ScriptClass::getScriptEngine() const { return internalState_.scriptEngine; }
ScriptClass::~ScriptClass() = default;

}  // namespace script
