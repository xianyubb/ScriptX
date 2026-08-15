#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "../../src/Engine.h"
#include "../../src/Exception.h"
#include "../../src/utils/MessageQueue.h"
#include "KotlinRuntime.h"

namespace script::kotlin_backend {

struct KotlinInterop {
  template <typename T>
  static Local<T> toLocal(KotlinValuePtr value) {
    return Local<T>(std::move(value));
  }

  template <typename T>
  static const KotlinValuePtr& value(const Local<T>& local) {
    return local.val_;
  }

  static Arguments makeArguments(KotlinEngine* engine, KotlinValuePtr thiz,
                                 std::vector<KotlinValuePtr> args);
};

class KotlinEngine : public ScriptEngine {
 public:
  explicit KotlinEngine(std::shared_ptr<utils::MessageQueue> queue = {});
  SCRIPTX_DISALLOW_COPY_AND_MOVE(KotlinEngine);

  void destroy() noexcept override;
  bool isDestroying() const override;
  Local<Value> get(const Local<String>& key) override;
  void set(const Local<String>& key, const Local<Value>& value) override;
  using ScriptEngine::set;
  Local<Value> eval(const Local<String>& script, const Local<String>& sourceFile) override;
  Local<Value> eval(const Local<String>& script) override;
  using ScriptEngine::eval;
  Local<Value> loadFile(const Local<String>& scriptFile) override;
  std::shared_ptr<utils::MessageQueue> messageQueue() override;
  void gc() override;
  size_t getHeapSize() override;
  void adjustAssociatedMemory(int64_t count) override;
  ScriptLanguage getLanguageType() override;
  std::string getEngineVersion() override;

  JniObject host() const { return host_; }
  KotlinValuePtr wrap(JniObject object) const { return wrapValue(object); }
  Local<Object> newNativeInstanceForExisting(void* pointer,
                                             const internal::ClassDefineState* classDefine);

 protected:
  ~KotlinEngine() override;
  void performRegisterNativeClass(internal::TypeIndex typeIndex,
                                  const internal::ClassDefineState* classDefine,
                                  ScriptClass* (*instanceTypeToScriptClass)(void*)) override;
  Local<Object> performNewNativeClass(internal::TypeIndex typeIndex,
                                      const internal::ClassDefineState* classDefine, size_t size,
                                      const Local<Value>* args) override;
  bool performIsInstanceOf(const Local<Value>& value,
                           const internal::ClassDefineState* classDefine) override;
  void* performGetNativeInstance(const Local<Value>& value,
                                 const internal::ClassDefineState* classDefine) override;

 private:
  std::shared_ptr<utils::MessageQueue> queue_;
  JniObject host_ = nullptr;
  std::atomic<int64_t> associatedMemory_{0};
  bool destroying_ = false;
};

}  // namespace script::kotlin_backend
