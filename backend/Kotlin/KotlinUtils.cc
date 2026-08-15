#include <ScriptX/ScriptX.h>
#include <jni.h>

#include "KotlinEngine.h"
#include "KotlinRuntime.h"

namespace script {

StringHolder::StringHolder(const Local<String>& string) : internalHolder_{} {
  const auto& value = kotlin_backend::KotlinInterop::value(string);
  if (!value || !value->object) return;
  auto& runtime = kotlin_backend::KotlinRuntime::instance();
  runtime.ensureStarted();
  auto* vm = static_cast<JavaVM*>(runtime.vm);
  JNIEnv* env = nullptr;
  const auto status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
  if (status == JNI_EDETACHED &&
      vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
    throw Exception("failed to attach to the JVM while reading a Kotlin String");
  }
  if (!env) throw Exception("failed to obtain JNI while reading a Kotlin String");
  auto javaString = static_cast<jstring>(value->object);
  const char* chars = env->GetStringUTFChars(javaString, nullptr);
  if (!chars) throw Exception("failed to read a Kotlin String");
  const auto length = static_cast<size_t>(env->GetStringUTFLength(javaString));
  internalHolder_.assign(chars, length);
  env->ReleaseStringUTFChars(javaString, chars);
}
StringHolder::~StringHolder() = default;
size_t StringHolder::length() const { return internalHolder_.size(); }
const char* StringHolder::c_str() const { return internalHolder_.c_str(); }
std::string_view StringHolder::stringView() const { return internalHolder_; }
std::string StringHolder::string() const { return internalHolder_; }

#if defined(__cpp_char8_t)
std::u8string StringHolder::u8string() const {
  return std::u8string(reinterpret_cast<const char8_t*>(internalHolder_.data()),
                       reinterpret_cast<const char8_t*>(internalHolder_.data()) +
                           internalHolder_.size());
}
std::u8string_view StringHolder::u8stringView() const {
  return std::u8string_view(reinterpret_cast<const char8_t*>(internalHolder_.data()),
                            internalHolder_.size());
}
const char8_t* StringHolder::c_u8str() const {
  return reinterpret_cast<const char8_t*>(internalHolder_.c_str());
}
#endif

}  // namespace script
