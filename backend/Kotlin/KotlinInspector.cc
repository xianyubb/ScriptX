#include "../../src/Inspector.h"
#include "../../src/Exception.h"

namespace script {

std::unique_ptr<ScriptInspector> ScriptInspector::create(
    ScriptEngine* engine, std::unique_ptr<InspectorAgent> agent, const Options& options) {
  static_cast<void>(engine);
  static_cast<void>(agent);
  static_cast<void>(options);
  throw Exception(
      "ScriptInspector is unavailable for the Kotlin/JVM backend; "
      "use the JVM's JDWP debugger for compiled Kotlin JARs");
}

}  // namespace script
