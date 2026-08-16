#pragma once

#include <utility>

#include "KotlinRuntime.h"

namespace script {

template <typename T>
Global<T>::Global() noexcept : val_() {}

template <typename T>
Global<T>::Global(const Local<T>& localReference) : val_(localReference.val_) {}

template <typename T>
Global<T>::Global(const Weak<T>& weakReference)
    : val_(kotlin_backend::lockWeakValue(weakReference.val_)) {}

template <typename T>
Global<T>::Global(const Global<T>& copy) : val_(copy.val_) {}

template <typename T>
Global<T>::Global(Global<T>&& move) noexcept : val_(std::move(move.val_)) {}

template <typename T>
Global<T>::~Global() = default;

template <typename T>
Global<T>& Global<T>::operator=(const Global<T>& assign) {
  Global(assign).swap(*this);
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(Global<T>&& move) noexcept {
  Global(std::move(move)).swap(*this);
  return *this;
}

template <typename T>
void Global<T>::swap(Global<T>& rhs) noexcept {
  val_.swap(rhs.val_);
}

template <typename T>
Global<T>& Global<T>::operator=(const Local<T>& assign) {
  val_ = assign.val_;
  return *this;
}

template <typename T>
Local<T> Global<T>::get() const {
  if (isEmpty()) throw Exception("get on empty Global");
  return Local<T>(val_);
}

template <typename T>
Local<Value> Global<T>::getValue() const {
  return Local<Value>(val_);
}

template <typename T>
bool Global<T>::isEmpty() const {
  return !val_;
}

template <typename T>
void Global<T>::reset() {
  val_.reset();
}

template <typename T>
Weak<T>::Weak() noexcept : val_() {}

template <typename T>
Weak<T>::~Weak() = default;

template <typename T>
Weak<T>::Weak(const Local<T>& localReference)
    : val_(kotlin_backend::makeWeakValue(localReference.val_)) {}

template <typename T>
Weak<T>::Weak(const Global<T>& globalReference)
    : val_(kotlin_backend::makeWeakValue(globalReference.val_)) {}

template <typename T>
Weak<T>::Weak(const Weak<T>& copy) : val_(copy.val_) {}

template <typename T>
Weak<T>::Weak(Weak<T>&& move) noexcept : val_(std::move(move.val_)) {}

template <typename T>
Weak<T>& Weak<T>::operator=(const Weak<T>& assign) {
  val_ = assign.val_;
  return *this;
}

template <typename T>
Weak<T>& Weak<T>::operator=(Weak<T>&& move) noexcept {
  val_ = std::move(move.val_);
  return *this;
}

template <typename T>
void Weak<T>::swap(Weak<T>& rhs) noexcept {
  val_.swap(rhs.val_);
}

template <typename T>
Weak<T>& Weak<T>::operator=(const Local<T>& assign) {
  val_ = assign.val_;
  return *this;
}

template <typename T>
Local<T> Weak<T>::get() const {
  auto value = kotlin_backend::lockWeakValue(val_);
  if (!value) throw Exception("get on empty or expired Weak");
  return Local<T>(std::move(value));
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
  return Local<Value>(kotlin_backend::lockWeakValue(val_));
}

template <typename T>
bool Weak<T>::isEmpty() const {
  return !kotlin_backend::lockWeakValue(val_);
}

template <typename T>
void Weak<T>::reset() noexcept {
  val_.reset();
}

}  // namespace script
