#include <ScriptX/ScriptX.h>
#include <jni.h>

#include <cstring>
#include <vector>

#include "KotlinEngine.h"

namespace script::kotlin_backend {
namespace {

JNIEnv* env() {
  auto& runtime = KotlinRuntime::instance();
  runtime.ensureStarted();
  auto* vm = static_cast<JavaVM*>(runtime.vm);
  JNIEnv* result = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&result), JNI_VERSION_1_8) == JNI_EDETACHED) {
    if (vm->AttachCurrentThread(reinterpret_cast<void**>(&result), nullptr) != JNI_OK)
      throw Exception("failed to attach to the JVM");
  }
  return result;
}

void check(JNIEnv* jni, const char* operation) {
  if (!jni->ExceptionCheck()) return;
  jthrowable throwable = jni->ExceptionOccurred();
  jni->ExceptionClear();
  jclass type = jni->GetObjectClass(throwable);
  jmethodID toString = jni->GetMethodID(type, "toString", "()Ljava/lang/String;");
  auto text = static_cast<jstring>(jni->CallObjectMethod(throwable, toString));
  const char* chars = text ? jni->GetStringUTFChars(text, nullptr) : nullptr;
  std::string message = chars ? chars : "Java exception";
  if (chars) jni->ReleaseStringUTFChars(text, chars);
  if (text) jni->DeleteLocalRef(text);
  jni->DeleteLocalRef(type);
  jni->DeleteLocalRef(throwable);
  throw Exception(std::string(operation) + ": " + message);
}

jobject object(const KotlinValuePtr& value) {
  return value ? static_cast<jobject>(value->object) : nullptr;
}

KotlinValuePtr boxedDouble(double value) {
  auto* jni = env();
  jclass type = jni->FindClass("java/lang/Double");
  jmethodID valueOf = jni->GetStaticMethodID(type, "valueOf", "(D)Ljava/lang/Double;");
  jobject result = jni->CallStaticObjectMethod(type, valueOf, value);
  jni->DeleteLocalRef(type);
  check(jni, "creating Kotlin Number");
  return wrapValue(result, ValueKind::kNumber);
}

}  // namespace

std::string describeValue(const KotlinValuePtr& value) {
  if (!value || !value->object) return "null";
  auto* jni = env();
  jclass type = jni->GetObjectClass(object(value));
  jmethodID toString = jni->GetMethodID(type, "toString", "()Ljava/lang/String;");
  auto text = static_cast<jstring>(jni->CallObjectMethod(object(value), toString));
  check(jni, "describing Kotlin value");
  const char* chars = jni->GetStringUTFChars(text, nullptr);
  std::string result = chars ? chars : "";
  if (chars) jni->ReleaseStringUTFChars(text, chars);
  jni->DeleteLocalRef(text);
  jni->DeleteLocalRef(type);
  return result;
}

}  // namespace script::kotlin_backend

namespace script {
using kotlin_backend::KotlinInterop;
using kotlin_backend::KotlinValuePtr;

#define KOTLIN_LOCAL_BASIC(ValueType)                                               \
  Local<ValueType>::Local(const Local<ValueType>& copy) : val_(copy.val_) {}         \
  Local<ValueType>::Local(Local<ValueType>&& move) noexcept                          \
      : val_(std::move(move.val_)) {}                                                \
  Local<ValueType>::~Local() = default;                                              \
  Local<ValueType>& Local<ValueType>::operator=(const Local& from) {                 \
    val_ = from.val_;                                                                \
    return *this;                                                                    \
  }                                                                                  \
  Local<ValueType>& Local<ValueType>::operator=(Local&& move) noexcept {             \
    val_ = std::move(move.val_);                                                      \
    return *this;                                                                    \
  }                                                                                  \
  void Local<ValueType>::swap(Local& rhs) noexcept { val_.swap(rhs.val_); }

#define KOTLIN_LOCAL_TYPED(ValueType)                                                \
  Local<ValueType>::Local(InternalLocalRef value) : val_(std::move(value)) {}         \
  Local<Value> Local<ValueType>::asValue() const {                                   \
    return KotlinInterop::toLocal<Value>(val_);                                      \
  }                                                                                  \
  bool Local<ValueType>::operator==(const Local<Value>& other) const {               \
    return asValue() == other;                                                       \
  }                                                                                  \
  Local<String> Local<ValueType>::describe() const {                                 \
    return String::newString(kotlin_backend::describeValue(val_));                   \
  }                                                                                  \
  std::string Local<ValueType>::describeUtf8() const {                               \
    return kotlin_backend::describeValue(val_);                                      \
  }

KOTLIN_LOCAL_BASIC(Value)
KOTLIN_LOCAL_BASIC(Object)
KOTLIN_LOCAL_BASIC(String)
KOTLIN_LOCAL_BASIC(Number)
KOTLIN_LOCAL_BASIC(Boolean)
KOTLIN_LOCAL_BASIC(Function)
KOTLIN_LOCAL_BASIC(Array)
KOTLIN_LOCAL_BASIC(ByteBuffer)
KOTLIN_LOCAL_BASIC(Unsupported)

KOTLIN_LOCAL_TYPED(Object)
KOTLIN_LOCAL_TYPED(String)
KOTLIN_LOCAL_TYPED(Number)
KOTLIN_LOCAL_TYPED(Boolean)
KOTLIN_LOCAL_TYPED(Function)
KOTLIN_LOCAL_TYPED(Array)
KOTLIN_LOCAL_TYPED(ByteBuffer)
KOTLIN_LOCAL_TYPED(Unsupported)

Local<Value>::Local() noexcept = default;
Local<Value>::Local(InternalLocalRef value) : val_(std::move(value)) {}
void Local<Value>::reset() { val_.reset(); }
bool Local<Value>::isNull() const { return !val_; }
ValueKind Local<Value>::getKind() const { return val_ ? val_->kind : ValueKind::kNull; }
bool Local<Value>::isString() const { return getKind() == ValueKind::kString; }
bool Local<Value>::isNumber() const { return getKind() == ValueKind::kNumber; }
bool Local<Value>::isBoolean() const { return getKind() == ValueKind::kBoolean; }
bool Local<Value>::isFunction() const { return getKind() == ValueKind::kFunction; }
bool Local<Value>::isArray() const { return getKind() == ValueKind::kArray; }
bool Local<Value>::isByteBuffer() const { return getKind() == ValueKind::kByteBuffer; }
bool Local<Value>::isObject() const { return getKind() == ValueKind::kObject; }
bool Local<Value>::isUnsupported() const { return getKind() == ValueKind::kUnsupported; }

#define KOTLIN_AS(ValueType, Check, Label)                        \
  Local<ValueType> Local<Value>::as##ValueType() const {          \
    if (!(Check)) throw Exception("can't cast value as " Label); \
    return KotlinInterop::toLocal<ValueType>(val_);               \
  }
KOTLIN_AS(Object, isObject(), "Object")
KOTLIN_AS(String, isString(), "String")
KOTLIN_AS(Number, isNumber(), "Number")
KOTLIN_AS(Boolean, isBoolean(), "Boolean")
KOTLIN_AS(Function, isFunction(), "Function")
KOTLIN_AS(Array, isArray(), "Array")
KOTLIN_AS(ByteBuffer, isByteBuffer(), "ByteBuffer")
KOTLIN_AS(Unsupported, isUnsupported(), "Unsupported")

bool Local<Value>::operator==(const Local<Value>& other) const {
  if (!val_ || !other.val_) return !val_ && !other.val_;
  auto* jni = kotlin_backend::env();
  if (jni->IsSameObject(kotlin_backend::object(val_), kotlin_backend::object(other.val_))) return true;
  jclass type = jni->GetObjectClass(kotlin_backend::object(val_));
  jmethodID equals = jni->GetMethodID(type, "equals", "(Ljava/lang/Object;)Z");
  const bool result = jni->CallBooleanMethod(kotlin_backend::object(val_), equals,
                                             kotlin_backend::object(other.val_)) == JNI_TRUE;
  jni->DeleteLocalRef(type);
  return result;
}

Local<String> Local<Value>::describe() const {
  return String::newString(kotlin_backend::describeValue(val_));
}

Local<Object> Object::newObject() {
  auto* jni = kotlin_backend::env();
  jclass type = jni->FindClass("java/util/LinkedHashMap");
  jmethodID constructor = jni->GetMethodID(type, "<init>", "()V");
  jobject value = jni->NewObject(type, constructor);
  jni->DeleteLocalRef(type);
  kotlin_backend::check(jni, "creating Kotlin Object");
  return KotlinInterop::toLocal<Object>(kotlin_backend::wrapValue(value, ValueKind::kObject));
}

Local<Object> Object::newObjectImpl(const Local<Value>& type, size_t size,
                                    const Local<Value>* args) {
  if (!type.isFunction()) throw Exception("Kotlin object type must be a Function");
  std::vector<Local<Value>> arguments;
  for (size_t i = 0; i < size; ++i) arguments.push_back(args[i]);
  return type.asFunction().call({}, arguments).asObject();
}

Local<String> String::newString(const char* utf8) {
  if (!utf8) return KotlinInterop::toLocal<String>({});
  return newString(std::string_view(utf8));
}
Local<String> String::newString(std::string_view utf8) {
  auto* jni = kotlin_backend::env();
  std::string copy(utf8);
  jstring value = jni->NewStringUTF(copy.c_str());
  kotlin_backend::check(jni, "creating Kotlin String");
  return KotlinInterop::toLocal<String>(kotlin_backend::wrapValue(value, ValueKind::kString));
}
Local<String> String::newString(const std::string& utf8) { return newString(std::string_view(utf8)); }
#if defined(__cpp_char8_t)
Local<String> String::newString(const char8_t* utf8) {
  return newString(reinterpret_cast<const char*>(utf8));
}
Local<String> String::newString(std::u8string_view utf8) {
  return newString(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
}
Local<String> String::newString(const std::u8string& utf8) { return newString(std::u8string_view(utf8)); }
#endif

Local<Number> Number::newNumber(float value) { return newNumber(static_cast<double>(value)); }
Local<Number> Number::newNumber(double value) {
  return KotlinInterop::toLocal<Number>(kotlin_backend::boxedDouble(value));
}
Local<Number> Number::newNumber(int32_t value) { return newNumber(static_cast<double>(value)); }
Local<Number> Number::newNumber(int64_t value) { return newNumber(static_cast<double>(value)); }

Local<Boolean> Boolean::newBoolean(bool value) {
  auto* jni = kotlin_backend::env();
  jclass type = jni->FindClass("java/lang/Boolean");
  jmethodID valueOf = jni->GetStaticMethodID(type, "valueOf", "(Z)Ljava/lang/Boolean;");
  jobject result = jni->CallStaticObjectMethod(type, valueOf, value ? JNI_TRUE : JNI_FALSE);
  jni->DeleteLocalRef(type);
  return KotlinInterop::toLocal<Boolean>(kotlin_backend::wrapValue(result, ValueKind::kBoolean));
}

Local<Function> Function::newFunction(FunctionCallback callback) {
  auto* engine = EngineScope::currentEngineAs<kotlin_backend::KotlinEngine>();
  if (!engine) throw Exception("creating Kotlin Function requires an EngineScope");
  auto function = kotlin_backend::createNativeFunction(engine, std::move(callback));
  return KotlinInterop::toLocal<Function>(
      kotlin_backend::wrapValue(function, ValueKind::kFunction));
}

Local<Array> Array::newArray(size_t size) {
  auto* jni = kotlin_backend::env();
  jclass type = jni->FindClass("java/util/ArrayList");
  jmethodID constructor = jni->GetMethodID(type, "<init>", "(I)V");
  jmethodID add = jni->GetMethodID(type, "add", "(Ljava/lang/Object;)Z");
  jobject array = jni->NewObject(type, constructor, static_cast<jint>(size));
  for (size_t i = 0; i < size; ++i) jni->CallBooleanMethod(array, add, nullptr);
  jni->DeleteLocalRef(type);
  return KotlinInterop::toLocal<Array>(kotlin_backend::wrapValue(array, ValueKind::kArray));
}

Local<Array> Array::newArrayImpl(size_t size, const Local<Value>* args) {
  auto result = newArray();
  for (size_t i = 0; i < size; ++i) result.add(args[i]);
  return result;
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(size_t size) {
  auto bytes = std::shared_ptr<void>(new uint8_t[size](), [](void* pointer) {
    delete[] static_cast<uint8_t*>(pointer);
  });
  return newByteBuffer(std::move(bytes), size);
}
Local<ByteBuffer> ByteBuffer::newByteBuffer(void* nativeBuffer, size_t size) {
  auto result = newByteBuffer(size);
  if (size) std::memcpy(result.getRawBytes(), nativeBuffer, size);
  result.commit();
  return result;
}
Local<ByteBuffer> ByteBuffer::newByteBuffer(std::shared_ptr<void> nativeBuffer, size_t size) {
  auto* jni = kotlin_backend::env();
  jbyteArray array = jni->NewByteArray(static_cast<jsize>(size));
  if (size) jni->SetByteArrayRegion(array, 0, static_cast<jsize>(size),
                                    static_cast<const jbyte*>(nativeBuffer.get()));
  auto value = kotlin_backend::wrapValue(array, ValueKind::kByteBuffer);
  value->nativeBytes = std::move(nativeBuffer);
  value->byteLength = size;
  return KotlinInterop::toLocal<ByteBuffer>(std::move(value));
}

Local<Value> Local<Object>::get(const Local<String>& key) const {
  auto* jni = kotlin_backend::env();
  jclass map = jni->FindClass("java/util/Map");
  jmethodID get = jni->GetMethodID(map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
  jobject result = jni->CallObjectMethod(kotlin_backend::object(val_), get,
                                         kotlin_backend::object(key.val_));
  jni->DeleteLocalRef(map);
  return KotlinInterop::toLocal<Value>(kotlin_backend::wrapValue(result));
}
void Local<Object>::set(const Local<String>& key, const Local<Value>& value) const {
  auto* jni = kotlin_backend::env();
  jclass map = jni->FindClass("java/util/Map");
  jmethodID put = jni->GetMethodID(map, "put",
                                  "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  jobject previous = jni->CallObjectMethod(kotlin_backend::object(val_), put,
                                           kotlin_backend::object(key.val_),
                                           kotlin_backend::object(value.val_));
  if (previous) jni->DeleteLocalRef(previous);
  jni->DeleteLocalRef(map);
}
void Local<Object>::remove(const Local<String>& key) const {
  auto* jni = kotlin_backend::env();
  jclass map = jni->FindClass("java/util/Map");
  jmethodID remove = jni->GetMethodID(map, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;");
  jobject previous = jni->CallObjectMethod(kotlin_backend::object(val_), remove,
                                           kotlin_backend::object(key.val_));
  if (previous) jni->DeleteLocalRef(previous);
  jni->DeleteLocalRef(map);
}
bool Local<Object>::has(const Local<String>& key) const {
  auto* jni = kotlin_backend::env();
  jclass map = jni->FindClass("java/util/Map");
  jmethodID contains = jni->GetMethodID(map, "containsKey", "(Ljava/lang/Object;)Z");
  bool result = jni->CallBooleanMethod(kotlin_backend::object(val_), contains,
                                       kotlin_backend::object(key.val_)) == JNI_TRUE;
  jni->DeleteLocalRef(map);
  return result;
}
bool Local<Object>::instanceOf(const Local<Value>& type) const {
  static_cast<void>(type);
  return false;
}
std::vector<Local<String>> Local<Object>::getKeys() const {
  auto* jni = kotlin_backend::env();
  jclass map = jni->FindClass("java/util/Map");
  jmethodID keySet = jni->GetMethodID(map, "keySet", "()Ljava/util/Set;");
  jobject set = jni->CallObjectMethod(kotlin_backend::object(val_), keySet);
  jclass collection = jni->FindClass("java/util/Collection");
  jmethodID toArray = jni->GetMethodID(collection, "toArray", "()[Ljava/lang/Object;");
  auto keys = static_cast<jobjectArray>(jni->CallObjectMethod(set, toArray));
  std::vector<Local<String>> result;
  const jsize count = jni->GetArrayLength(keys);
  result.reserve(static_cast<size_t>(count));
  for (jsize i = 0; i < count; ++i) {
    jobject key = jni->GetObjectArrayElement(keys, i);
    auto wrapped = kotlin_backend::wrapValue(key);
    if (wrapped->kind == ValueKind::kString)
      result.push_back(KotlinInterop::toLocal<String>(std::move(wrapped)));
  }
  jni->DeleteLocalRef(keys);
  jni->DeleteLocalRef(set);
  jni->DeleteLocalRef(collection);
  jni->DeleteLocalRef(map);
  return result;
}

double Local<Number>::toDouble() const {
  auto* jni = kotlin_backend::env();
  jclass number = jni->FindClass("java/lang/Number");
  jmethodID method = jni->GetMethodID(number, "doubleValue", "()D");
  double result = jni->CallDoubleMethod(kotlin_backend::object(val_), method);
  jni->DeleteLocalRef(number);
  return result;
}
float Local<Number>::toFloat() const { return static_cast<float>(toDouble()); }
int32_t Local<Number>::toInt32() const { return static_cast<int32_t>(toDouble()); }
int64_t Local<Number>::toInt64() const { return static_cast<int64_t>(toDouble()); }
bool Local<Boolean>::value() const {
  auto* jni = kotlin_backend::env();
  jclass boolean = jni->FindClass("java/lang/Boolean");
  jmethodID method = jni->GetMethodID(boolean, "booleanValue", "()Z");
  bool result = jni->CallBooleanMethod(kotlin_backend::object(val_), method) == JNI_TRUE;
  jni->DeleteLocalRef(boolean);
  return result;
}

Local<Value> Local<Function>::callImpl(const Local<Value>& thiz, size_t size,
                                       const Local<Value>* args) const {
  static_cast<void>(thiz);
  auto* jni = kotlin_backend::env();
  jclass type = jni->GetObjectClass(kotlin_backend::object(val_));
  jmethodID invoke = jni->GetMethodID(type, "invoke", "([Ljava/lang/Object;)Ljava/lang/Object;");
  jclass objectClass = jni->FindClass("java/lang/Object");
  jobjectArray javaArgs = jni->NewObjectArray(static_cast<jsize>(size), objectClass, nullptr);
  for (size_t i = 0; i < size; ++i)
    jni->SetObjectArrayElement(javaArgs, static_cast<jsize>(i), kotlin_backend::object(args[i].val_));
  jobject result = jni->CallObjectMethod(kotlin_backend::object(val_), invoke, javaArgs);
  kotlin_backend::check(jni, "calling Kotlin Function");
  jni->DeleteLocalRef(javaArgs);
  jni->DeleteLocalRef(objectClass);
  jni->DeleteLocalRef(type);
  return KotlinInterop::toLocal<Value>(kotlin_backend::wrapValue(result));
}

size_t Local<Array>::size() const {
  auto* jni = kotlin_backend::env();
  jclass list = jni->FindClass("java/util/List");
  jmethodID method = jni->GetMethodID(list, "size", "()I");
  auto result = static_cast<size_t>(jni->CallIntMethod(kotlin_backend::object(val_), method));
  jni->DeleteLocalRef(list);
  return result;
}
Local<Value> Local<Array>::get(size_t index) const {
  auto* jni = kotlin_backend::env();
  jclass list = jni->FindClass("java/util/List");
  jmethodID method = jni->GetMethodID(list, "get", "(I)Ljava/lang/Object;");
  jobject result = jni->CallObjectMethod(kotlin_backend::object(val_), method,
                                         static_cast<jint>(index));
  jni->DeleteLocalRef(list);
  kotlin_backend::check(jni, "getting Kotlin Array element");
  return KotlinInterop::toLocal<Value>(kotlin_backend::wrapValue(result));
}
void Local<Array>::set(size_t index, const Local<Value>& value) const {
  auto* jni = kotlin_backend::env();
  jclass list = jni->FindClass("java/util/List");
  jmethodID sizeMethod = jni->GetMethodID(list, "size", "()I");
  jmethodID addMethod = jni->GetMethodID(list, "add", "(Ljava/lang/Object;)Z");
  jmethodID setMethod = jni->GetMethodID(list, "set", "(ILjava/lang/Object;)Ljava/lang/Object;");
  jint current = jni->CallIntMethod(kotlin_backend::object(val_), sizeMethod);
  while (current <= static_cast<jint>(index)) {
    jni->CallBooleanMethod(kotlin_backend::object(val_), addMethod, nullptr);
    ++current;
  }
  jobject previous = jni->CallObjectMethod(kotlin_backend::object(val_), setMethod,
                                           static_cast<jint>(index),
                                           kotlin_backend::object(value.val_));
  if (previous) jni->DeleteLocalRef(previous);
  jni->DeleteLocalRef(list);
}
void Local<Array>::add(const Local<Value>& value) const {
  auto* jni = kotlin_backend::env();
  jclass list = jni->FindClass("java/util/List");
  jmethodID method = jni->GetMethodID(list, "add", "(Ljava/lang/Object;)Z");
  jni->CallBooleanMethod(kotlin_backend::object(val_), method, kotlin_backend::object(value.val_));
  jni->DeleteLocalRef(list);
}
void Local<Array>::clear() const {
  auto* jni = kotlin_backend::env();
  jclass list = jni->FindClass("java/util/List");
  jmethodID method = jni->GetMethodID(list, "clear", "()V");
  jni->CallVoidMethod(kotlin_backend::object(val_), method);
  jni->DeleteLocalRef(list);
}

ByteBuffer::Type Local<ByteBuffer>::getType() const { return ByteBuffer::Type::kInt8; }
bool Local<ByteBuffer>::isShared() const { return false; }
void Local<ByteBuffer>::commit() const {
  if (!val_->nativeBytes || !val_->byteLength) return;
  kotlin_backend::env()->SetByteArrayRegion(
      static_cast<jbyteArray>(val_->object), 0, static_cast<jsize>(val_->byteLength),
      static_cast<const jbyte*>(val_->nativeBytes.get()));
}
void Local<ByteBuffer>::sync() const {
  if (!val_->nativeBytes || !val_->byteLength) return;
  kotlin_backend::env()->GetByteArrayRegion(static_cast<jbyteArray>(val_->object), 0,
                                           static_cast<jsize>(val_->byteLength),
                                           static_cast<jbyte*>(val_->nativeBytes.get()));
}
size_t Local<ByteBuffer>::byteLength() const { return val_->byteLength; }
void* Local<ByteBuffer>::getRawBytes() const { return val_->nativeBytes.get(); }
std::shared_ptr<void> Local<ByteBuffer>::getRawBytesShared() const { return val_->nativeBytes; }

}  // namespace script
