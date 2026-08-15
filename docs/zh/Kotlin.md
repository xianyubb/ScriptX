# Kotlin 后端

Kotlin 后端在宿主进程内嵌入一个 JVM，通过 Kotlin JSR-223 `main-kts` 引擎执行 Kotlin/JVM
脚本，配置方式与其他后端一致：

```sh
cmake -S . -B build -DSCRIPTX_BACKEND=Kotlin
```

选择 Kotlin 后端时，CMake 会检查 `PATH` 中的 `kotlinc`/`kotlinc.bat`，并将 Kotlin 发行版的
运行时 JAR 加入 JVM classpath；运行程序的机器需要兼容的 JDK/JRE（包含 JNI/JVM）。选择
其他后端不需要安装 Kotlin 或 JVM。

`ScriptEngine::set` 会把基础值、数组、对象、字节缓冲区和原生函数导出到同一个 JVM 脚本
环境，`eval` 与 `loadFile` 会把结果转换回 ScriptX 值。原生函数回调通过 JNI 同步回到 C++，
因此不需要启动外部 `kotlinc` 进程。JVM 自己负责对象生命周期和 GC；`gc()` 不能强制控制
JVM，`ByteBuffer` 使用显式的 copy/commit/sync 语义。

`registerNativeClass/newNativeClass` 会生成 Kotlin 可见的类包装：构造、静态/实例函数、
静态/实例属性，以及 C++ 侧的 `isInstanceOf/getNativeInstance` 均通过 JNI 映射。类包装会在
每次脚本求值前注入到 Kotlin 编译单元，因此请避免使用以 `__scriptx_native_` 开头的标识符。
由于 C++ 回调没有 Kotlin 的静态返回类型，绑定函数和属性在 Kotlin 中的类型为 `Any?`；将其
用于 Kotlin 的特定类型运算前，请显式转换（例如 `(Box.twice(2) as Number).toInt()`）。
