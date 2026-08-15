#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "../../src/Value.h"
#include "../../src/types.h"

namespace script::kotlin_backend {

class KotlinEngine;

using JniObject = void*;
using JniVm = void*;

struct KotlinValue {
  JniObject object = nullptr;
  JniVm vm = nullptr;
  ValueKind kind = ValueKind::kNull;
  std::shared_ptr<void> nativeBytes;
  size_t byteLength = 0;

  KotlinValue() = default;
  KotlinValue(JniObject localObject, JniVm javaVm, ValueKind valueKind)
      : object(localObject), vm(javaVm), kind(valueKind) {}
  ~KotlinValue();
  KotlinValue(const KotlinValue&) = delete;
  KotlinValue& operator=(const KotlinValue&) = delete;
};

using KotlinValuePtr = std::shared_ptr<KotlinValue>;

struct KotlinRuntime {
  JniVm vm = nullptr;
  static KotlinRuntime& instance();
  void ensureStarted();
  JniObject createHost();
  void destroyHost(JniObject host);
  JniObject eval(JniObject host, const std::string& source, const std::string& file);
  JniObject get(JniObject host, const std::string& name);
  void set(JniObject host, const std::string& name, JniObject value);
  void addPrelude(JniObject host, const std::string& source);
  JniObject newNativeInstance(JniObject host, int64_t pointer, int64_t classId);
  void setNativeInstancePointer(JniObject object, int64_t pointer);
  int64_t nativeInstancePointer(JniObject object);
  int64_t nativeInstanceClassId(JniObject object);
  std::string version() const;
};

KotlinValuePtr wrapValue(JniObject localObject, ValueKind expected = ValueKind::kUnsupported);
ValueKind detectKind(JniObject object);
std::string describeValue(const KotlinValuePtr& value);
JniObject createNativeFunction(KotlinEngine* engine, FunctionCallback callback);
JniObject createNativeConstructor(KotlinEngine* engine, int64_t classId,
                                  InstanceConstructor callback);
void releaseNativeFunctions(KotlinEngine* engine);

}  // namespace script::kotlin_backend
