# Kotlin backend

The Kotlin backend embeds one JVM in the host process. `ScriptEngine::loadFile` is JAR-only:
it loads a compiled Kotlin/JVM `.jar` and invokes the standard JAR `Main-Class` entry point.
It is selected like the other backends:

```sh
cmake -S . -B build -DSCRIPTX_BACKEND=Kotlin
```

When the Kotlin backend is selected, CMake checks for `kotlinc`/`kotlinc.bat` on `PATH` and builds
the runtime JAR classpath. The deployed application needs a compatible JDK/JRE with JNI/JVM;
other backends do not require Kotlin or a JVM installation.

`ScriptEngine::set` exports primitive values, arrays, objects, byte buffers, and native functions
into the JVM host. `eval` remains available for in-memory diagnostics, while `loadFile` refuses
`.kt`/`.kts` source files and accepts only compiled JARs. Native function callbacks synchronously
cross JNI into C++, so no external `kotlinc` process is started. `gc()` requests JVM collection,
and direct `ByteBuffer` values share their native backing memory without copy/commit/sync steps.

Value, array, object, function, native-function callback, and native-class behavior is available
in-process. `registerNativeClass/newNativeClass` generates Kotlin-visible wrappers for
constructors, static/instance functions, static/instance properties, and the C++
`isInstanceOf/getNativeInstance` APIs. The wrappers are injected before every script evaluation;
avoid identifiers beginning with `__scriptx_native_`.
Because C++ callbacks have no Kotlin static return type, bound functions and properties have the
Kotlin type `Any?`; cast before using a value in a Kotlin-specific operation (for example,
`(Box.twice(2) as Number).toInt()`). ScriptX's V8 Inspector protocol is not implemented by the
JVM backend; use the JVM JDWP debugger for compiled JARs instead.
