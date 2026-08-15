# Kotlin backend

The Kotlin backend embeds one JVM in the host process and evaluates Kotlin/JVM scripts through
the Kotlin JSR-223 `main-kts` engine. It is selected like the other backends:

```sh
cmake -S . -B build -DSCRIPTX_BACKEND=Kotlin
```

When the Kotlin backend is selected, CMake checks for `kotlinc`/`kotlinc.bat` on `PATH` and builds
the runtime JAR classpath. The deployed application needs a compatible JDK/JRE with JNI/JVM;
other backends do not require Kotlin or a JVM installation.

`ScriptEngine::set` exports primitive values, arrays, objects, byte buffers, and native functions
into the same JVM script environment, while `eval` and `loadFile` convert results back to ScriptX
values. Native function callbacks synchronously cross JNI into C++, so no external `kotlinc`
process is started. The JVM owns object lifetime and garbage collection; `gc()` cannot force JVM
collection, and `ByteBuffer` uses explicit copy/commit/sync semantics.

Value, array, object, function, native-function callback, and native-class behavior is available
in-process. `registerNativeClass/newNativeClass` generates Kotlin-visible wrappers for
constructors, static/instance functions, static/instance properties, and the C++
`isInstanceOf/getNativeInstance` APIs. The wrappers are injected before every script evaluation;
avoid identifiers beginning with `__scriptx_native_`.
Because C++ callbacks have no Kotlin static return type, bound functions and properties have the
Kotlin type `Any?`; cast before using a value in a Kotlin-specific operation (for example,
`(Box.twice(2) as Number).toInt()`).
