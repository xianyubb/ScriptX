#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
  bool directByteBuffer = false;

  KotlinValue() = default;
  KotlinValue(JniObject localObject, JniVm javaVm, ValueKind valueKind)
      : object(localObject), vm(javaVm), kind(valueKind) {}
  ~KotlinValue();
  KotlinValue(const KotlinValue&) = delete;
  KotlinValue& operator=(const KotlinValue&) = delete;
};

using KotlinValuePtr = std::shared_ptr<KotlinValue>;

struct KotlinWeakValue {
  JniObject object = nullptr;
  JniVm vm = nullptr;

  KotlinWeakValue() = default;
  KotlinWeakValue(JniObject weakObject, JniVm javaVm) : object(weakObject), vm(javaVm) {}
  ~KotlinWeakValue();
  KotlinWeakValue(const KotlinWeakValue&) = delete;
  KotlinWeakValue& operator=(const KotlinWeakValue&) = delete;
};

using KotlinWeakValuePtr = std::shared_ptr<KotlinWeakValue>;

struct KotlinRuntime {
  JniVm vm = nullptr;
  static KotlinRuntime& instance();
  void ensureStarted();
  JniObject createHost();
  void destroyHost(JniObject host);
  JniObject eval(JniObject host, const std::string& source, const std::string& file);
  JniObject get(JniObject host, const std::string& name);
  void set(JniObject host, const std::string& name, JniObject value);
  void setNative(JniObject host, const std::string& name, JniObject value);
  void addPrelude(JniObject host, const std::string& source);
  JniObject call(JniObject host, const std::string& name,
                 const std::vector<JniObject>& args);
  JniObject callInstance(JniObject host, const std::string& className,
                         const std::string& method, JniObject receiver,
                         const std::vector<JniObject>& args);
  JniObject construct(JniObject host, const std::string& className,
                      const std::vector<JniObject>& args);
  void registerNativeClass(JniObject host, int64_t classId, const std::string& className);
  std::string nativeClassName(JniObject host, JniObject value);
  JniObject loadJar(JniObject host, const std::string& jarPath);
  void gc(JniObject host);
  size_t heapSize(JniObject host);
  int64_t loadCompiledPlugin(JniObject host, const std::string& jarPath,
                             const std::string& mainClass, const std::string& pluginName);
  void enableCompiledPlugin(JniObject host, int64_t pluginHandle);
  void unloadCompiledPlugin(JniObject host, int64_t pluginHandle);
  JniObject newNativeInstance(JniObject host, int64_t pointer, int64_t classId);
  void setNativeInstancePointer(JniObject object, int64_t pointer);
  int64_t nativeInstancePointer(JniObject object);
  int64_t nativeInstanceClassId(JniObject object);
  std::string version() const;
};

KotlinValuePtr wrapValue(JniObject localObject, ValueKind expected = ValueKind::kUnsupported);
KotlinWeakValuePtr makeWeakValue(const KotlinValuePtr& value);
KotlinValuePtr lockWeakValue(const KotlinWeakValuePtr& value);
ValueKind detectKind(JniObject object);
std::string describeValue(const KotlinValuePtr& value);
JniObject createNativeFunction(KotlinEngine* engine, FunctionCallback callback);
JniObject createNativeConstructor(KotlinEngine* engine, int64_t classId,
                                  InstanceConstructor callback);
void releaseNativeFunctions(KotlinEngine* engine);

}  // namespace script::kotlin_backend
