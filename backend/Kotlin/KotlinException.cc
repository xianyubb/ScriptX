#include <ScriptX/ScriptX.h>

namespace script {

Exception::Exception(std::string message) : std::exception(), exception_{} {
  exception_.message = std::move(message);
  exception_.stacktrace = "[Kotlin backend]";
}

Exception::Exception(const Local<String>& message) : Exception(message.toString()) {}

Exception::Exception(const Local<Value>& exception) : Exception(exception.describeUtf8()) {}

Local<Value> Exception::exception() const { return {}; }
std::string Exception::message() const noexcept { return exception_.message; }
std::string Exception::stacktrace() const noexcept { return exception_.stacktrace; }
const char* Exception::what() const noexcept { return exception_.message.c_str(); }

}  // namespace script
