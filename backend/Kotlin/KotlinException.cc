#include <ScriptX/ScriptX.h>
#include <jni.h>

#include "KotlinRuntime.h"

namespace script {

Exception::Exception(std::string message) : std::exception(), exception_{} {
  constexpr std::string_view stackMarker = "\n[Kotlin JVM stack]\n";
  const auto marker = message.find(stackMarker);
  if (marker == std::string::npos) {
    exception_.message = std::move(message);
    exception_.stacktrace = "[Kotlin backend]";
  } else {
    exception_.message = message.substr(0, marker);
    exception_.stacktrace = message.substr(marker + stackMarker.size());
  }
}

Exception::Exception(const Local<String>& message) : Exception(message.toString()) {}

Exception::Exception(const Local<Value>& exception) : Exception(exception.describeUtf8()) {}

Local<Value> Exception::exception() const {
  try {
    auto& runtime = kotlin_backend::KotlinRuntime::instance();
    runtime.ensureStarted();
    auto* vm = static_cast<JavaVM*>(runtime.vm);
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) == JNI_EDETACHED &&
        vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
      return {};
    }
    if (!env) return {};
    auto type = env->FindClass("java/lang/RuntimeException");
    auto constructor = env->GetMethodID(type, "<init>", "(Ljava/lang/String;)V");
    auto message = env->NewStringUTF(exception_.message.c_str());
    auto exception = env->NewObject(type, constructor, message);
    env->DeleteLocalRef(message);
    env->DeleteLocalRef(type);
    return kotlin_backend::KotlinInterop::toLocal<Value>(
        kotlin_backend::wrapValue(exception, ValueKind::kObject));
  } catch (...) {
    return {};
  }
}
std::string Exception::message() const noexcept { return exception_.message; }
std::string Exception::stacktrace() const noexcept { return exception_.stacktrace; }
const char* Exception::what() const noexcept { return exception_.message.c_str(); }

}  // namespace script
