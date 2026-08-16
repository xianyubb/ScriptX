#include "KotlinRuntime.h"

#include <ScriptX/ScriptX.h>
#include <jni.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "KotlinEngine.h"

#ifndef SCRIPTX_KOTLIN_CLASSPATH
#define SCRIPTX_KOTLIN_CLASSPATH ""
#endif

namespace script::kotlin_backend {
namespace {

std::mutex runtimeMutex;
jclass hostClass = nullptr;
jclass nativeFunctionClass = nullptr;
jclass nativeConstructorClass = nullptr;
jclass nativeInstanceClass = nullptr;
jclass nativeBridgeClass = nullptr;
jmethodID hostConstructor = nullptr;
jmethodID hostEval = nullptr;
jmethodID hostGet = nullptr;
jmethodID hostSet = nullptr;
jmethodID hostSetNative = nullptr;
jmethodID hostClose = nullptr;
jmethodID hostAddPrelude = nullptr;
jmethodID hostCall = nullptr;
jmethodID hostCallInstance = nullptr;
jmethodID hostConstruct = nullptr;
jmethodID hostRegisterNativeClass = nullptr;
jmethodID hostNativeClassName = nullptr;
jmethodID hostLoadJar = nullptr;
jmethodID hostGc = nullptr;
jmethodID hostHeapSize = nullptr;
jmethodID hostLoadCompiledPlugin = nullptr;
jmethodID hostEnableCompiledPlugin = nullptr;
jmethodID hostUnloadCompiledPlugin = nullptr;
jmethodID hostNewNativeInstance = nullptr;
jmethodID nativeFunctionConstructor = nullptr;
jmethodID nativeConstructorConstructor = nullptr;
jmethodID nativeInstancePointerMethod = nullptr;
jmethodID nativeInstanceClassIdMethod = nullptr;
jmethodID nativeInstanceSetPointerMethod = nullptr;

struct CallbackEntry {
  KotlinEngine* engine;
  FunctionCallback callback;
};
struct ConstructorEntry {
  KotlinEngine* engine;
  int64_t classId;
  InstanceConstructor constructor;
};
std::mutex callbackMutex;
std::unordered_map<jlong, CallbackEntry> callbacks;
std::unordered_map<jlong, ConstructorEntry> constructors;
std::atomic<jlong> nextCallbackId{1};

JNIEnv* environment(JavaVM* vm) {
  JNIEnv* env = nullptr;
  const auto status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
  if (status == JNI_EDETACHED) {
#ifdef _WIN32
    if (vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
#else
    if (vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
#endif
      throw Exception("failed to attach the current thread to the JVM");
    }
  } else if (status != JNI_OK) {
    throw Exception("failed to obtain a JNI environment");
  }
  return env;
}

std::string javaString(JNIEnv* env, jstring string) {
  if (!string) return {};
  const char* chars = env->GetStringUTFChars(string, nullptr);
  if (!chars) return {};
  std::string result(chars);
  env->ReleaseStringUTFChars(string, chars);
  return result;
}

std::string pendingJavaException(JNIEnv* env) {
  jthrowable throwable = env->ExceptionOccurred();
  if (!throwable) return {};
  env->ExceptionClear();
  jclass throwableClass = env->GetObjectClass(throwable);
  jmethodID toString = env->GetMethodID(throwableClass, "toString", "()Ljava/lang/String;");
  auto text = static_cast<jstring>(env->CallObjectMethod(throwable, toString));
  std::string result = javaString(env, text);
  jclass writerClass = env->FindClass("java/io/StringWriter");
  jclass printWriterClass = env->FindClass("java/io/PrintWriter");
  jmethodID writerConstructor = env->GetMethodID(writerClass, "<init>", "()V");
  jmethodID printWriterConstructor =
      env->GetMethodID(printWriterClass, "<init>", "(Ljava/io/Writer;)V");
  jmethodID printStackTrace = env->GetMethodID(
      throwableClass, "printStackTrace", "(Ljava/io/PrintWriter;)V");
  jmethodID writerToString = env->GetMethodID(writerClass, "toString", "()Ljava/lang/String;");
  jobject writer = env->NewObject(writerClass, writerConstructor);
  jobject printWriter = env->NewObject(printWriterClass, printWriterConstructor, writer);
  env->CallVoidMethod(throwable, printStackTrace, printWriter);
  auto stack = static_cast<jstring>(env->CallObjectMethod(writer, writerToString));
  const auto stackText = javaString(env, stack);
  if (!stackText.empty()) result += "\n[Kotlin JVM stack]\n" + stackText;
  if (stack) env->DeleteLocalRef(stack);
  env->DeleteLocalRef(printWriter);
  env->DeleteLocalRef(writer);
  env->DeleteLocalRef(printWriterClass);
  env->DeleteLocalRef(writerClass);
  env->DeleteLocalRef(text);
  env->DeleteLocalRef(throwableClass);
  env->DeleteLocalRef(throwable);
  return result;
}

void checkJavaException(JNIEnv* env, const char* operation) {
  if (!env->ExceptionCheck()) return;
  const auto detail = pendingJavaException(env);
  throw Exception(std::string(operation) + (detail.empty() ? " failed" : ": " + detail));
}

jobjectArray javaArguments(JNIEnv* env, const std::vector<JniObject>& args) {
  jclass objectClass = env->FindClass("java/lang/Object");
  checkJavaException(env, "loading java.lang.Object");
  jobjectArray result = env->NewObjectArray(static_cast<jsize>(args.size()), objectClass, nullptr);
  for (jsize index = 0; index < static_cast<jsize>(args.size()); ++index) {
    if (args[static_cast<size_t>(index)]) {
      env->SetObjectArrayElement(
          result, index, static_cast<jobject>(args[static_cast<size_t>(index)]));
    }
  }
  env->DeleteLocalRef(objectClass);
  checkJavaException(env, "creating Kotlin API arguments");
  return result;
}

jobject toLocalObject(JNIEnv* env, const KotlinValuePtr& value) {
  if (!value || !value->object) return nullptr;
  return env->NewLocalRef(static_cast<jobject>(value->object));
}

jobject unwrapNativeInstance(JNIEnv* env, jobject object) {
  if (!object) return nullptr;
  if (env->IsInstanceOf(object, nativeInstanceClass)) return env->NewLocalRef(object);
  jclass type = env->GetObjectClass(object);
  jmethodID unwrap = env->GetMethodID(type, "__scriptxNativeInstance",
                                      "()LScriptXKotlinHost$NativeInstance;");
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    env->DeleteLocalRef(type);
    return nullptr;
  }
  jobject result = env->CallObjectMethod(object, unwrap);
  env->DeleteLocalRef(type);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return nullptr;
  }
  return result;
}

jobject JNICALL nativeInvoke(JNIEnv* env, jclass, jlong callbackId, jobjectArray arguments) {
  try {
    CallbackEntry entry;
    {
      std::lock_guard lock(callbackMutex);
      auto iterator = callbacks.find(callbackId);
      if (iterator == callbacks.end()) throw Exception("Kotlin native callback has expired");
      entry = iterator->second;
    }

    std::vector<KotlinValuePtr> values;
    const jsize count = arguments ? env->GetArrayLength(arguments) : 0;
    values.reserve(static_cast<size_t>(count));
    for (jsize index = 0; index < count; ++index) {
      jobject value = env->GetObjectArrayElement(arguments, index);
      values.push_back(wrapValue(value));
    }

    EngineScope scope(entry.engine);
    auto args = KotlinInterop::makeArguments(entry.engine, {}, std::move(values));
    auto result = entry.callback(args);
    return toLocalObject(env, KotlinInterop::value(result));
  } catch (const std::exception& exception) {
    jclass runtimeException = env->FindClass("java/lang/RuntimeException");
    env->ThrowNew(runtimeException, exception.what());
    env->DeleteLocalRef(runtimeException);
    return nullptr;
  }
}

jobject JNICALL nativeConstruct(JNIEnv* env, jclass, jlong callbackId, jlong classId,
                                jobjectArray arguments) {
  try {
    ConstructorEntry entry;
    {
      std::lock_guard lock(callbackMutex);
      auto iterator = constructors.find(callbackId);
      if (iterator == constructors.end()) throw Exception("Kotlin native constructor has expired");
      entry = iterator->second;
    }
    std::vector<KotlinValuePtr> values;
    const jsize count = arguments ? env->GetArrayLength(arguments) : 0;
    values.reserve(static_cast<size_t>(count));
    for (jsize index = 0; index < count; ++index)
      values.push_back(wrapValue(env->GetObjectArrayElement(arguments, index)));
    jclass type = env->FindClass("ScriptXKotlinHost$NativeInstance");
    jmethodID ctor = env->GetMethodID(type, "<init>", "(JJ)V");
    jobject instance = env->NewObject(type, ctor, static_cast<jlong>(0), classId);
    env->DeleteLocalRef(type);
    auto receiver = wrapValue(env->NewLocalRef(instance), ValueKind::kObject);
    EngineScope scope(entry.engine);
    auto args = KotlinInterop::makeArguments(entry.engine, receiver, std::move(values));
    void* pointer = entry.constructor(args);
    env->CallVoidMethod(instance, nativeInstanceSetPointerMethod, reinterpret_cast<jlong>(pointer));
    checkJavaException(env, "constructing a Kotlin native class");
    return instance;
  } catch (const std::exception& exception) {
    jclass runtimeException = env->FindClass("java/lang/RuntimeException");
    env->ThrowNew(runtimeException, exception.what());
    env->DeleteLocalRef(runtimeException);
    return nullptr;
  }
}

void initializeClasses(JNIEnv* env) {
  jclass localHost = env->FindClass("ScriptXKotlinHost");
  checkJavaException(env, "loading ScriptXKotlinHost");
  hostClass = static_cast<jclass>(env->NewGlobalRef(localHost));
  env->DeleteLocalRef(localHost);
  hostConstructor = env->GetMethodID(hostClass, "<init>", "()V");
  hostEval = env->GetMethodID(hostClass, "eval",
                              "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;");
  hostGet = env->GetMethodID(hostClass, "get", "(Ljava/lang/String;)Ljava/lang/Object;");
  hostSet = env->GetMethodID(hostClass, "set", "(Ljava/lang/String;Ljava/lang/Object;)V");
  hostSetNative = env->GetMethodID(hostClass, "setNative",
                                   "(Ljava/lang/String;Ljava/lang/Object;)V");
  hostClose = env->GetMethodID(hostClass, "close", "()V");
  hostAddPrelude = env->GetMethodID(hostClass, "addPrelude", "(Ljava/lang/String;)V");
  hostCall = env->GetMethodID(
      hostClass, "call", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
  hostCallInstance = env->GetMethodID(
      hostClass, "callInstance",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
  hostConstruct = env->GetMethodID(
      hostClass, "construct", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
  hostRegisterNativeClass =
      env->GetMethodID(hostClass, "registerNativeClass", "(JLjava/lang/String;)V");
  hostNativeClassName =
      env->GetMethodID(hostClass, "nativeClassName", "(Ljava/lang/Object;)Ljava/lang/String;");
  hostLoadJar = env->GetMethodID(hostClass, "loadJar", "(Ljava/lang/String;)Ljava/lang/Object;");
  hostGc = env->GetMethodID(hostClass, "gc", "()V");
  hostHeapSize = env->GetMethodID(hostClass, "heapSize", "()J");
  hostLoadCompiledPlugin = env->GetMethodID(
      hostClass, "loadCompiledPlugin",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)J");
  hostEnableCompiledPlugin =
      env->GetMethodID(hostClass, "enableCompiledPlugin", "(J)V");
  hostUnloadCompiledPlugin = env->GetMethodID(hostClass, "unloadCompiledPlugin", "(J)V");
  hostNewNativeInstance = env->GetMethodID(hostClass, "newNativeInstance", "(JJ)Ljava/lang/Object;");

  jclass localFunction = env->FindClass("ScriptXKotlinHost$NativeFunction");
  checkJavaException(env, "loading ScriptXKotlinHost.NativeFunction");
  nativeFunctionClass = static_cast<jclass>(env->NewGlobalRef(localFunction));
  env->DeleteLocalRef(localFunction);
  nativeFunctionConstructor = env->GetMethodID(nativeFunctionClass, "<init>", "(J)V");
  jclass localConstructor = env->FindClass("ScriptXKotlinHost$NativeConstructor");
  checkJavaException(env, "loading ScriptXKotlinHost.NativeConstructor");
  nativeConstructorClass = static_cast<jclass>(env->NewGlobalRef(localConstructor));
  env->DeleteLocalRef(localConstructor);
  nativeConstructorConstructor = env->GetMethodID(nativeConstructorClass, "<init>", "(JJ)V");
  jclass localInstance = env->FindClass("ScriptXKotlinHost$NativeInstance");
  checkJavaException(env, "loading ScriptXKotlinHost.NativeInstance");
  nativeInstanceClass = static_cast<jclass>(env->NewGlobalRef(localInstance));
  env->DeleteLocalRef(localInstance);
  nativeInstancePointerMethod = env->GetMethodID(nativeInstanceClass, "pointer", "()J");
  nativeInstanceClassIdMethod = env->GetMethodID(nativeInstanceClass, "classId", "()J");
  nativeInstanceSetPointerMethod = env->GetMethodID(nativeInstanceClass, "setPointer", "(J)V");

  jclass localBridge = env->FindClass("ScriptXKotlinHost$NativeBridge");
  checkJavaException(env, "loading ScriptXKotlinHost.NativeBridge");
  nativeBridgeClass = static_cast<jclass>(env->NewGlobalRef(localBridge));
  env->DeleteLocalRef(localBridge);

  JNINativeMethod method{const_cast<char*>("nativeInvoke"),
                         const_cast<char*>("(J[Ljava/lang/Object;)Ljava/lang/Object;"),
                         reinterpret_cast<void*>(nativeInvoke)};
  if (env->RegisterNatives(nativeBridgeClass, &method, 1) != JNI_OK) {
    checkJavaException(env, "registering Kotlin native callbacks");
    throw Exception("failed to register Kotlin native callbacks");
  }
  JNINativeMethod constructorMethod{const_cast<char*>("nativeConstruct"),
                                    const_cast<char*>("(JJ[Ljava/lang/Object;)Ljava/lang/Object;"),
                                    reinterpret_cast<void*>(nativeConstruct)};
  if (env->RegisterNatives(nativeBridgeClass, &constructorMethod, 1) != JNI_OK) {
    checkJavaException(env, "registering Kotlin native constructors");
    throw Exception("failed to register Kotlin native constructors");
  }
}

}  // namespace

KotlinRuntime& KotlinRuntime::instance() {
  static KotlinRuntime runtime;
  return runtime;
}

void KotlinRuntime::ensureStarted() {
  std::lock_guard lock(runtimeMutex);
  if (vm) return;

  JavaVM* javaVm = nullptr;
  jsize count = 0;
  if (JNI_GetCreatedJavaVMs(&javaVm, 1, &count) != JNI_OK) {
    throw Exception("failed to query existing JVMs");
  }

  JNIEnv* env = nullptr;
  if (count == 0) {
    std::string classPath = SCRIPTX_KOTLIN_CLASSPATH;
    const std::string separatorToken = "__SCRIPTX_KOTLIN_PATHSEP__";
    auto separatorPosition = classPath.find(separatorToken);
    while (separatorPosition != std::string::npos) {
#ifdef _WIN32
      classPath.replace(separatorPosition, separatorToken.size(), ";");
#else
      classPath.replace(separatorPosition, separatorToken.size(), ":");
#endif
      separatorPosition = classPath.find(separatorToken);
    }
    classPath = "-Djava.class.path=" + classPath;
    std::string stackSize = "-Xss8m";
    JavaVMOption options[] = {{classPath.data(), nullptr}, {stackSize.data(), nullptr}};
    JavaVMInitArgs arguments{};
    arguments.version = JNI_VERSION_1_8;
    arguments.nOptions = 2;
    arguments.options = options;
    arguments.ignoreUnrecognized = JNI_FALSE;
    if (JNI_CreateJavaVM(&javaVm, reinterpret_cast<void**>(&env), &arguments) != JNI_OK) {
      throw Exception("failed to create the embedded JVM");
    }
  } else {
    env = environment(javaVm);
  }

  initializeClasses(env);
  vm = javaVm;
}

JniObject KotlinRuntime::createHost() {
  ensureStarted();
  auto* env = environment(static_cast<JavaVM*>(vm));
  jobject local = env->NewObject(hostClass, hostConstructor);
  checkJavaException(env, "creating the Kotlin scripting engine");
  jobject global = env->NewGlobalRef(local);
  env->DeleteLocalRef(local);
  return global;
}

void KotlinRuntime::destroyHost(JniObject host) {
  if (!host || !vm) return;
  auto* env = environment(static_cast<JavaVM*>(vm));
  env->CallVoidMethod(static_cast<jobject>(host), hostClose);
  // Destruction is noexcept at the ScriptEngine boundary. Clear any exception
  // raised while stopping the worker so it cannot poison later JNI calls.
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteGlobalRef(static_cast<jobject>(host));
}

JniObject KotlinRuntime::eval(JniObject host, const std::string& source, const std::string& file) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaSource = env->NewStringUTF(source.c_str());
  jstring javaFile = env->NewStringUTF(file.c_str());
  jobject result = env->CallObjectMethod(static_cast<jobject>(host), hostEval, javaSource, javaFile);
  env->DeleteLocalRef(javaSource);
  env->DeleteLocalRef(javaFile);
  checkJavaException(env, "evaluating Kotlin script");
  return result;
}

JniObject KotlinRuntime::get(JniObject host, const std::string& name) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaName = env->NewStringUTF(name.c_str());
  jobject result = env->CallObjectMethod(static_cast<jobject>(host), hostGet, javaName);
  env->DeleteLocalRef(javaName);
  checkJavaException(env, "getting a Kotlin global");
  return result;
}

void KotlinRuntime::set(JniObject host, const std::string& name, JniObject value) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaName = env->NewStringUTF(name.c_str());
  env->CallVoidMethod(static_cast<jobject>(host), hostSet, javaName, static_cast<jobject>(value));
  env->DeleteLocalRef(javaName);
  checkJavaException(env, "setting a Kotlin global");
}

void KotlinRuntime::setNative(JniObject host, const std::string& name, JniObject value) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaName = env->NewStringUTF(name.c_str());
  env->CallVoidMethod(static_cast<jobject>(host), hostSetNative, javaName,
                      static_cast<jobject>(value));
  env->DeleteLocalRef(javaName);
  checkJavaException(env, "registering a Kotlin native callback");
}

void KotlinRuntime::addPrelude(JniObject host, const std::string& source) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaSource = env->NewStringUTF(source.c_str());
  env->CallVoidMethod(static_cast<jobject>(host), hostAddPrelude, javaSource);
  env->DeleteLocalRef(javaSource);
  checkJavaException(env, "registering Kotlin native class");
}

JniObject KotlinRuntime::call(JniObject host, const std::string& name,
                               const std::vector<JniObject>& args) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaName = env->NewStringUTF(name.c_str());
  jobjectArray javaArgs = javaArguments(env, args);
  jobject result = env->CallObjectMethod(
      static_cast<jobject>(host), hostCall, javaName, javaArgs);
  env->DeleteLocalRef(javaName);
  env->DeleteLocalRef(javaArgs);
  checkJavaException(env, "calling a Kotlin-exported ScriptX API");
  return result;
}

JniObject KotlinRuntime::callInstance(JniObject host, const std::string& className,
                                       const std::string& method, JniObject receiver,
                                       const std::vector<JniObject>& args) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaClassName = env->NewStringUTF(className.c_str());
  jstring javaMethod = env->NewStringUTF(method.c_str());
  jobjectArray javaArgs = javaArguments(env, args);
  jobject result = env->CallObjectMethod(
      static_cast<jobject>(host), hostCallInstance, javaClassName, javaMethod,
      static_cast<jobject>(receiver), javaArgs);
  env->DeleteLocalRef(javaClassName);
  env->DeleteLocalRef(javaMethod);
  env->DeleteLocalRef(javaArgs);
  checkJavaException(env, "calling a Kotlin-exported ScriptX instance API");
  return result;
}

JniObject KotlinRuntime::construct(JniObject host, const std::string& className,
                                    const std::vector<JniObject>& args) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaClassName = env->NewStringUTF(className.c_str());
  jobjectArray javaArgs = javaArguments(env, args);
  jobject result = env->CallObjectMethod(
      static_cast<jobject>(host), hostConstruct, javaClassName, javaArgs);
  env->DeleteLocalRef(javaClassName);
  env->DeleteLocalRef(javaArgs);
  checkJavaException(env, "constructing a Kotlin-exported ScriptX class");
  return result;
}

void KotlinRuntime::registerNativeClass(JniObject host, int64_t classId,
                                        const std::string& className) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaClassName = env->NewStringUTF(className.c_str());
  env->CallVoidMethod(static_cast<jobject>(host), hostRegisterNativeClass,
                      static_cast<jlong>(classId), javaClassName);
  env->DeleteLocalRef(javaClassName);
  checkJavaException(env, "registering a Kotlin-exported ScriptX class");
}

std::string KotlinRuntime::nativeClassName(JniObject host, JniObject value) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  auto result = static_cast<jstring>(
      env->CallObjectMethod(static_cast<jobject>(host), hostNativeClassName,
                            static_cast<jobject>(value)));
  checkJavaException(env, "reading a Kotlin native class name");
  auto name = javaString(env, result);
  if (result) env->DeleteLocalRef(result);
  return name;
}

JniObject KotlinRuntime::loadJar(JniObject host, const std::string& jarPath) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaJarPath = env->NewStringUTF(jarPath.c_str());
  jobject result = env->CallObjectMethod(static_cast<jobject>(host), hostLoadJar, javaJarPath);
  env->DeleteLocalRef(javaJarPath);
  checkJavaException(env, "loading Kotlin JAR");
  return result;
}

void KotlinRuntime::gc(JniObject host) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  env->CallVoidMethod(static_cast<jobject>(host), hostGc);
  checkJavaException(env, "requesting Kotlin JVM garbage collection");
}

size_t KotlinRuntime::heapSize(JniObject host) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  const auto result = env->CallLongMethod(static_cast<jobject>(host), hostHeapSize);
  checkJavaException(env, "reading Kotlin JVM heap size");
  return result <= 0 ? 0 : static_cast<size_t>(result);
}

int64_t KotlinRuntime::loadCompiledPlugin(JniObject host, const std::string& jarPath,
                                          const std::string& mainClass,
                                          const std::string& pluginName) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jstring javaJarPath = env->NewStringUTF(jarPath.c_str());
  jstring javaMainClass = env->NewStringUTF(mainClass.c_str());
  jstring javaPluginName = env->NewStringUTF(pluginName.c_str());
  const auto result = static_cast<int64_t>(
      env->CallLongMethod(static_cast<jobject>(host), hostLoadCompiledPlugin, javaJarPath,
                          javaMainClass, javaPluginName));
  env->DeleteLocalRef(javaJarPath);
  env->DeleteLocalRef(javaMainClass);
  env->DeleteLocalRef(javaPluginName);
  checkJavaException(env, "loading compiled Kotlin plugin");
  return result;
}

void KotlinRuntime::enableCompiledPlugin(JniObject host, int64_t pluginHandle) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  env->CallVoidMethod(static_cast<jobject>(host), hostEnableCompiledPlugin,
                      static_cast<jlong>(pluginHandle));
  checkJavaException(env, "enabling compiled Kotlin plugin");
}

void KotlinRuntime::unloadCompiledPlugin(JniObject host, int64_t pluginHandle) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  env->CallVoidMethod(static_cast<jobject>(host), hostUnloadCompiledPlugin,
                      static_cast<jlong>(pluginHandle));
  checkJavaException(env, "unloading compiled Kotlin plugin");
}

JniObject KotlinRuntime::newNativeInstance(JniObject host, int64_t pointer, int64_t classId) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jobject result = env->CallObjectMethod(static_cast<jobject>(host), hostNewNativeInstance,
                                         static_cast<jlong>(pointer), static_cast<jlong>(classId));
  checkJavaException(env, "creating a Kotlin native instance");
  return result;
}

void KotlinRuntime::setNativeInstancePointer(JniObject object, int64_t pointer) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  env->CallVoidMethod(static_cast<jobject>(object), nativeInstanceSetPointerMethod,
                      static_cast<jlong>(pointer));
  checkJavaException(env, "updating a Kotlin native instance");
}

int64_t KotlinRuntime::nativeInstancePointer(JniObject object) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jobject instance = unwrapNativeInstance(env, static_cast<jobject>(object));
  if (!instance) return 0;
  const auto result = static_cast<int64_t>(env->CallLongMethod(instance, nativeInstancePointerMethod));
  env->DeleteLocalRef(instance);
  return result;
}

int64_t KotlinRuntime::nativeInstanceClassId(JniObject object) {
  auto* env = environment(static_cast<JavaVM*>(vm));
  jobject instance = unwrapNativeInstance(env, static_cast<jobject>(object));
  if (!instance) return 0;
  const auto result = static_cast<int64_t>(env->CallLongMethod(instance, nativeInstanceClassIdMethod));
  env->DeleteLocalRef(instance);
  return result;
}

std::string KotlinRuntime::version() const { return "Kotlin/JVM embedded JSR-223"; }

KotlinValue::~KotlinValue() {
  if (!object || !vm) return;
  try {
    environment(static_cast<JavaVM*>(vm))->DeleteGlobalRef(static_cast<jobject>(object));
  } catch (...) {
  }
}

KotlinWeakValue::~KotlinWeakValue() {
  if (!object || !vm) return;
  try {
    environment(static_cast<JavaVM*>(vm))->DeleteWeakGlobalRef(static_cast<jweak>(object));
  } catch (...) {
  }
}

KotlinValuePtr wrapValue(JniObject localObject, ValueKind expected) {
  if (!localObject) return {};
  auto& runtime = KotlinRuntime::instance();
  runtime.ensureStarted();
  auto* env = environment(static_cast<JavaVM*>(runtime.vm));
  auto kind = expected == ValueKind::kUnsupported ? detectKind(localObject) : expected;
  jobject global = env->NewGlobalRef(static_cast<jobject>(localObject));
  auto result = std::make_shared<KotlinValue>(global, runtime.vm, kind);
  if (kind == ValueKind::kByteBuffer) {
    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");
    const bool isByteBuffer =
        byteBufferClass && env->IsInstanceOf(static_cast<jobject>(localObject), byteBufferClass);
    const bool isDirectBuffer =
        isByteBuffer && env->GetDirectBufferAddress(static_cast<jobject>(localObject));
    if (isDirectBuffer) {
      const auto size = env->GetDirectBufferCapacity(static_cast<jobject>(localObject));
      result->byteLength = size > 0 ? static_cast<size_t>(size) : 0;
      result->directByteBuffer = true;
    } else if (isByteBuffer) {
      const auto capacityMethod = env->GetMethodID(byteBufferClass, "capacity", "()I");
      const auto duplicateMethod = env->GetMethodID(
          byteBufferClass, "duplicate", "()Ljava/nio/ByteBuffer;");
      const auto clearMethod = env->GetMethodID(byteBufferClass, "clear", "()Ljava/nio/Buffer;");
      const auto getMethod = env->GetMethodID(byteBufferClass, "get", "([B)Ljava/nio/ByteBuffer;");
      const auto capacity = env->CallIntMethod(static_cast<jobject>(localObject), capacityMethod);
      checkJavaException(env, "reading Kotlin ByteBuffer capacity");
      const auto size = capacity > 0 ? static_cast<size_t>(capacity) : 0;
      auto bytes = env->NewByteArray(static_cast<jsize>(size));
      auto duplicate = env->CallObjectMethod(static_cast<jobject>(localObject), duplicateMethod);
      auto cleared = env->CallObjectMethod(duplicate, clearMethod);
      if (cleared) env->DeleteLocalRef(cleared);
      if (size) {
        auto read = env->CallObjectMethod(duplicate, getMethod, bytes);
        if (read) env->DeleteLocalRef(read);
      }
      checkJavaException(env, "copying Kotlin ByteBuffer");
      result->nativeBytes = std::shared_ptr<void>(new uint8_t[size](), [](void* pointer) {
        delete[] static_cast<uint8_t*>(pointer);
      });
      result->byteLength = size;
      if (size) {
        env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(size),
                                static_cast<jbyte*>(result->nativeBytes.get()));
      }
      env->DeleteLocalRef(duplicate);
      env->DeleteLocalRef(bytes);
    } else {
      const auto size = static_cast<size_t>(env->GetArrayLength(static_cast<jbyteArray>(localObject)));
      result->nativeBytes = std::shared_ptr<void>(new uint8_t[size](), [](void* pointer) {
        delete[] static_cast<uint8_t*>(pointer);
      });
      result->byteLength = size;
      if (size) env->GetByteArrayRegion(static_cast<jbyteArray>(localObject), 0,
                                        static_cast<jsize>(size),
                                        static_cast<jbyte*>(result->nativeBytes.get()));
    }
    if (byteBufferClass) env->DeleteLocalRef(byteBufferClass);
  }
  env->DeleteLocalRef(static_cast<jobject>(localObject));
  return result;
}

KotlinWeakValuePtr makeWeakValue(const KotlinValuePtr& value) {
  if (!value || !value->object || !value->vm) return {};
  auto* env = environment(static_cast<JavaVM*>(value->vm));
  auto weak = env->NewWeakGlobalRef(static_cast<jobject>(value->object));
  if (!weak) return {};
  return std::make_shared<KotlinWeakValue>(weak, value->vm);
}

KotlinValuePtr lockWeakValue(const KotlinWeakValuePtr& value) {
  if (!value || !value->object || !value->vm) return {};
  auto* env = environment(static_cast<JavaVM*>(value->vm));
  auto local = env->NewLocalRef(static_cast<jweak>(value->object));
  if (!local) return {};
  return wrapValue(local);
}

ValueKind detectKind(JniObject object) {
  if (!object) return ValueKind::kNull;
  auto& runtime = KotlinRuntime::instance();
  auto* env = environment(static_cast<JavaVM*>(runtime.vm));
  struct TypeCheck {
    const char* name;
    ValueKind kind;
  } checks[] = {{"java/lang/String", ValueKind::kString},
                {"java/lang/Boolean", ValueKind::kBoolean},
                {"java/lang/Number", ValueKind::kNumber},
                {"java/util/List", ValueKind::kArray},
                {"java/util/Map", ValueKind::kObject},
                {"ScriptXKotlinHost$NativeInstance", ValueKind::kObject},
                {"java/nio/ByteBuffer", ValueKind::kByteBuffer},
                {"[B", ValueKind::kByteBuffer},
                {"kotlin/Function", ValueKind::kFunction},
                {"ScriptXKotlinHost$NativeFunction", ValueKind::kFunction}};
  for (const auto& check : checks) {
    jclass type = env->FindClass(check.name);
    const bool matches = type && env->IsInstanceOf(static_cast<jobject>(object), type);
    if (type) env->DeleteLocalRef(type);
    if (matches) return check.kind;
  }
  jclass type = env->GetObjectClass(static_cast<jobject>(object));
  jmethodID unwrap = env->GetMethodID(type, "__scriptxNativeInstance",
                                      "()LScriptXKotlinHost$NativeInstance;");
  const bool isNativeWrapper = !env->ExceptionCheck() && unwrap != nullptr;
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(type);
  if (isNativeWrapper) return ValueKind::kObject;
  return ValueKind::kUnsupported;
}

JniObject createNativeFunction(KotlinEngine* engine, FunctionCallback callback) {
  auto& runtime = KotlinRuntime::instance();
  runtime.ensureStarted();
  auto* env = environment(static_cast<JavaVM*>(runtime.vm));
  const jlong id = nextCallbackId.fetch_add(1);
  {
    std::lock_guard lock(callbackMutex);
    callbacks.emplace(id, CallbackEntry{engine, std::move(callback)});
  }
  jobject function = env->NewObject(nativeFunctionClass, nativeFunctionConstructor, id);
  checkJavaException(env, "creating a Kotlin native function");
  return function;
}

JniObject createNativeConstructor(KotlinEngine* engine, int64_t classId,
                                  InstanceConstructor callback) {
  auto& runtime = KotlinRuntime::instance();
  runtime.ensureStarted();
  auto* env = environment(static_cast<JavaVM*>(runtime.vm));
  const jlong id = nextCallbackId.fetch_add(1);
  {
    std::lock_guard lock(callbackMutex);
    constructors.emplace(id, ConstructorEntry{engine, classId, std::move(callback)});
  }
  jobject result = env->NewObject(nativeConstructorClass, nativeConstructorConstructor, id,
                                  static_cast<jlong>(classId));
  checkJavaException(env, "creating a Kotlin native constructor");
  return result;
}

void releaseNativeFunctions(KotlinEngine* engine) {
  std::lock_guard lock(callbackMutex);
  for (auto iterator = callbacks.begin(); iterator != callbacks.end();) {
    if (iterator->second.engine == engine)
      iterator = callbacks.erase(iterator);
    else
      ++iterator;
  }
  for (auto iterator = constructors.begin(); iterator != constructors.end();) {
    if (iterator->second.engine == engine)
      iterator = constructors.erase(iterator);
    else
      ++iterator;
  }
}

}  // namespace script::kotlin_backend
