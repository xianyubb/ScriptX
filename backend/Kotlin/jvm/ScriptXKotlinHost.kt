import org.legacy.scriptengine.kotlin.runtime.ScriptXKotlinRuntime
import java.io.File
import java.lang.reflect.InvocationHandler
import java.lang.reflect.Proxy
import java.net.URLClassLoader
import java.util.LinkedHashMap
import java.util.concurrent.Callable
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory
import java.util.concurrent.atomic.AtomicLong
import java.util.jar.Attributes
import java.util.jar.JarFile
import javax.script.ScriptEngine
import javax.script.ScriptEngineFactory
import javax.script.ScriptEngineManager

/**
 * Generic JVM host used by ScriptX's Kotlin backend.
 *
 * LSE-specific plugin interfaces intentionally stay outside ScriptX. The host
 * discovers them through reflection and exposes the currently bound ScriptX
 * API through ScriptXKotlinRuntime.Dispatcher.
 */
class ScriptXKotlinHost : AutoCloseable {
    private val preludes = ArrayList<String>()
    @Volatile private var engineThread: Thread? = null
    private val executor: ExecutorService = Executors.newSingleThreadExecutor(
        ThreadFactory { runnable ->
            Thread(runnable, "ScriptX-Kotlin").also {
                it.isDaemon = true
                engineThread = it
            }
        }
    )
    private var engine: ScriptEngine? = null
    private val nextPluginHandle = AtomicLong(1)
    private val loadedPlugins = LinkedHashMap<Long, LoadedPlugin>()
    private val nativeClassNames = ConcurrentHashMap<Long, String>()
    private val nativeBindings = ConcurrentHashMap<String, Any?>()
    private val globalBindings = ConcurrentHashMap<String, Any?>()
    private var closed = false

    private data class LoadedPlugin(
        val name: String,
        val classLoader: URLClassLoader,
        val plugin: Any,
        val pluginInterface: Class<*>,
        val contextType: Class<*>,
        val context: Any,
        var enabled: Boolean = false
    )

    private fun <T> onEngineThread(action: () -> T): T {
        if (Thread.currentThread() === engineThread) return action()
        try {
            return executor.submit(Callable { action() }).get()
        } catch (exception: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted while waiting for Kotlin", exception)
        } catch (exception: java.util.concurrent.ExecutionException) {
            val cause = exception.cause
            when (cause) {
                is RuntimeException -> throw cause
                is Error -> throw cause
                else -> throw IllegalStateException(cause)
            }
        }
    }

    private fun createEngine(): ScriptEngine {
        val candidate = try {
            val factoryType = Class.forName(
                "org.jetbrains.kotlin.mainKts.jsr223.KotlinJsr223MainKtsScriptEngineFactory"
            )
            (factoryType.getDeclaredConstructor().newInstance() as ScriptEngineFactory).scriptEngine
        } catch (_: ReflectiveOperationException) {
            val manager = ScriptEngineManager()
            manager.getEngineByName("kotlin") ?: manager.getEngineByExtension("kts")
        }
        return candidate
            ?: throw IllegalStateException(
                "Kotlin JSR-223 engine is unavailable; kotlin-main-kts.jar is required"
            )
    }

    private fun scriptEngine(): ScriptEngine {
        engine?.let { return it }
        val created = createEngine()
        created.put("__scriptxHost", this)
        globalBindings.forEach { (name, value) -> created.put(name, value) }
        nativeBindings.forEach { (name, value) -> created.put(name, value) }
        engine = created
        return created
    }

    fun eval(source: String, sourceFile: String): Any? = onEngineThread {
        @Suppress("UNUSED_VARIABLE")
        val ignoredSourceFile = sourceFile
        scriptEngine().eval(buildSource(source))
    }

    private fun buildSource(source: String): String = buildString {
        preludes.forEach { append(it).append('\n') }
        append(source)
    }

    fun addPrelude(source: String) {
        onEngineThread { preludes.add(source) }
    }

    fun newNativeInstance(pointer: Long, classId: Long): Any =
        NativeInstance(pointer, classId)

    fun get(name: String): Any? = onEngineThread {
        engine?.get(name) ?: nativeBindings[name] ?: globalBindings[name]
    }

    fun set(name: String, value: Any?) {
        onEngineThread {
            if (value == null) globalBindings.remove(name) else globalBindings[name] = value
            engine?.put(name, value)
        }
    }

    fun setNative(name: String, value: Any?) {
        if (value == null) nativeBindings.remove(name) else nativeBindings[name] = value
        onEngineThread { engine?.put(name, value) }
    }

    /** Invokes a ScriptX function exported into this engine. */
    fun call(name: String, vararg args: Any?): Any? = onEngineThread {
        val function = scriptEngine()[name]
            ?: throw IllegalStateException("ScriptX function is not exported: $name")
        invokeFunction(function, args)
    }

    fun callInstance(className: String, method: String, receiver: Any?, vararg args: Any?): Any? =
        onEngineThread {
            val native = receiver as? NativeInstance
                ?: throw IllegalArgumentException("not a ScriptX native instance")
            call(
                "__scriptx_api_${className}_i_$method",
                native.pointer(),
                native,
                *args
            )
        }

    fun getInstanceProperty(className: String, property: String, receiver: Any?): Any? =
        callInstance(className, "${property}_get", receiver)

    fun setInstanceProperty(className: String, property: String, receiver: Any?, value: Any?) {
        callInstance(className, "${property}_set", receiver, value)
    }

    fun construct(className: String, vararg args: Any?): Any? =
        call("__scriptx_api_${className}_ctor", *args)

    fun registerNativeClass(classId: Long, className: String) {
        onEngineThread { nativeClassNames[classId] = className }
    }

    fun nativeClassName(value: Any?): String? {
        val native = value as? NativeInstance ?: return null
        return onEngineThread { nativeClassNames[native.classId()] }
    }

    private fun nativeClassNameDirect(value: Any?): String? {
        val native = value as? NativeInstance ?: return null
        return nativeClassNames[native.classId()]
    }

    private fun invokeFunction(function: Any, args: Array<out Any?>): Any? {
        if (function is NativeFunction) return function.invoke(*args)
        val method = function.javaClass.methods.firstOrNull {
            it.name == "invoke" && it.parameterCount == 1 &&
                it.parameterTypes[0].isArray
        } ?: throw IllegalStateException(
            "ScriptX export is not callable: ${function.javaClass.name}"
        )
        return method.invoke(function, args)
    }

    private fun callDirect(name: String, vararg args: Any?): Any? {
        val function = nativeBindings[name]
            ?: throw IllegalStateException("ScriptX native API is not exported: $name")
        return invokeFunction(function, args)
    }

    private fun callInstanceDirect(
        className: String,
        method: String,
        receiver: Any?,
        vararg args: Any?
    ): Any? {
        val native = receiver as? NativeInstance
            ?: throw IllegalArgumentException("not a ScriptX native instance")
        return callDirect(
            "__scriptx_api_${className}_i_$method",
            native.pointer(),
            native,
            *args
        )
    }

    private fun constructDirect(className: String, vararg args: Any?): Any? =
        callDirect("__scriptx_api_${className}_ctor", *args)

    /** Runs a callback while LSE Kotlin wrappers are bound to this engine. */
    private fun <T> withApiScope(directNativeCalls: Boolean = false, action: () -> T): T {
        val dispatcher = object : ScriptXKotlinRuntime.Dispatcher {
            override fun call(name: String, args: Array<Any?>): Any? =
                if (directNativeCalls) {
                    this@ScriptXKotlinHost.callDirect(name, *args)
                } else {
                    this@ScriptXKotlinHost.call(name, *args)
                }

            override fun callInstance(
                className: String,
                method: String,
                receiver: Any?,
                args: Array<Any?>
            ): Any? =
                if (directNativeCalls) {
                    this@ScriptXKotlinHost.callInstanceDirect(className, method, receiver, *args)
                } else {
                    this@ScriptXKotlinHost.callInstance(className, method, receiver, *args)
                }

            override fun construct(className: String, args: Array<Any?>): Any? =
                if (directNativeCalls) {
                    this@ScriptXKotlinHost.constructDirect(className, *args)
                } else {
                    this@ScriptXKotlinHost.construct(className, *args)
                }

            override fun nativeClassName(value: Any?): String? =
                if (directNativeCalls) {
                    this@ScriptXKotlinHost.nativeClassNameDirect(value)
                } else {
                    this@ScriptXKotlinHost.nativeClassName(value)
                }
        }
        ScriptXKotlinRuntime.bind(dispatcher)
        return try {
            action()
        } finally {
            ScriptXKotlinRuntime.clear()
        }
    }

    /**
     * Runs a conventional Main-Class from a JAR. JAR-only loading is
     * intentional: compiled bytecode makes the API boundary deterministic.
     */
    fun loadJar(jarPath: String): Any? = onEngineThread {
        val jarFile = File(jarPath)
        require(jarFile.isFile && jarFile.extension.equals("jar", ignoreCase = true)) {
            "Kotlin backend accepts compiled .jar files only: $jarPath"
        }
        val mainClass = JarFile(jarFile).use { jar ->
            jar.manifest?.mainAttributes?.getValue(Attributes.Name.MAIN_CLASS)
                ?: throw IllegalArgumentException("Kotlin JAR has no Main-Class: $jarPath")
        }
        URLClassLoader(arrayOf(jarFile.toURI().toURL()), javaClass.classLoader).use { loader ->
            val entry = Class.forName(mainClass, true, loader)
            val main = entry.getMethod("main", Array<String>::class.java)
            main.invoke(null, emptyArray<String>())
        }
        null
    }

    fun gc() {
        onEngineThread { Runtime.getRuntime().gc() }
    }

    fun heapSize(): Long = onEngineThread { Runtime.getRuntime().totalMemory() }

    fun loadCompiledPlugin(jarPath: String, mainClass: String, pluginName: String): Long =
        onEngineThread {
            check(!closed) { "Kotlin host is closed" }
            val jarFile = File(jarPath)
            require(jarFile.isFile && jarFile.extension.equals("jar", ignoreCase = true)) {
                "Kotlin plugin must be a compiled .jar: $jarPath"
            }
            val loader = URLClassLoader(arrayOf(jarFile.toURI().toURL()), javaClass.classLoader)
            try {
                val pluginType = Class.forName(mainClass, true, loader)
                val pluginInterface = Class.forName(
                    "org.legacy.scriptengine.kotlin.api.LseKotlinPlugin",
                    true,
                    javaClass.classLoader
                )
                val plugin = pluginType.getDeclaredConstructor().newInstance()
                require(pluginInterface.isInstance(plugin)) {
                    "$mainClass does not implement ${pluginInterface.name}"
                }
                val contextType = Class.forName(
                    "org.legacy.scriptengine.kotlin.api.KotlinPluginContext",
                    true,
                    javaClass.classLoader
                )
                val context = createPluginContext(pluginName, contextType)
                val handle = nextPluginHandle.getAndIncrement()
                val loaded = LoadedPlugin(
                    pluginName,
                    loader,
                    plugin,
                    pluginInterface,
                    contextType,
                    context
                )
                loadedPlugins[handle] = loaded
                handle
            } catch (exception: Throwable) {
                loader.close()
                throw exception
            }
        }

    /**
     * Enables a previously loaded compiled plugin.
     *
     * LSE calls this after its mod-loading transaction has completed. Keeping
     * this separate from loadCompiledPlugin is important because onEnable may
     * call mc.listen, which enters LeviLamina's mod/event registry.
     */
    fun enableCompiledPlugin(pluginHandle: Long) {
        val loaded = loadedPlugins[pluginHandle]
            ?: throw IllegalArgumentException("unknown Kotlin plugin handle: $pluginHandle")
        if (loaded.enabled) return
        withApiScope(directNativeCalls = true) {
            loaded.pluginInterface
                .getMethod("onEnable", loaded.contextType)
                .invoke(loaded.plugin, loaded.context)
        }
        loaded.enabled = true
    }

    private fun createPluginContext(
        pluginName: String,
        contextType: Class<*>
    ): Any {
        val apiVersion = Class.forName(
            "org.legacy.scriptengine.kotlin.api.LseKotlinPlugin",
            true,
            javaClass.classLoader
        ).getField("API_VERSION").get(null) as String
        val handler = InvocationHandler { _, method, args ->
            when (method.name) {
                "apiVersion" -> apiVersion
                "log" -> {
                    println("[LSE Kotlin/$pluginName] ${args?.getOrNull(0)}")
                    null
                }
                else -> null
            }
        }
        return Proxy.newProxyInstance(
            contextType.classLoader,
            arrayOf(contextType),
            handler
        )
    }

    fun unloadCompiledPlugin(pluginHandle: Long) {
        val loaded = loadedPlugins.remove(pluginHandle) ?: return
        try {
            if (loaded.enabled) {
                withApiScope(directNativeCalls = true) {
                    loaded.pluginInterface.getMethod("onDisable").invoke(loaded.plugin)
                }
            }
        } finally {
            loaded.classLoader.close()
        }
    }

    override fun close() {
        if (closed) return
        onEngineThread {
            if (closed) return@onEngineThread
            loadedPlugins.keys.toList().forEach { unloadCompiledPlugin(it) }
            closed = true
        }
        executor.shutdownNow()
    }

    fun version(): String = "Kotlin/JVM (embedded JSR-223, JAR-only plugins)"

    class NativeFunction(private val callback: Long) {
        @Suppress("UNCHECKED_CAST")
        fun invoke(vararg args: Any?): Any? =
            NativeBridge.nativeInvoke(callback, args as Array<Any?>)
    }

    class NativeConstructor(private val callback: Long, private val classId: Long) {
        @Suppress("UNCHECKED_CAST")
        fun invoke(vararg args: Any?): Any? =
            NativeBridge.nativeConstruct(callback, classId, args as Array<Any?>)
    }

    /** Opaque native instance holder. It is a Map so ScriptX sees it as an Object. */
    class NativeInstance(
        private var nativePointer: Long,
        private val nativeClassId: Long
    ) : LinkedHashMap<String, Any?>() {
        fun pointer(): Long = nativePointer
        fun classId(): Long = nativeClassId
        fun setPointer(pointer: Long) {
            nativePointer = pointer
        }
    }

    private object NativeBridge {
        external fun nativeInvoke(callback: Long, args: Array<Any?>): Any?
        external fun nativeConstruct(callback: Long, classId: Long, args: Array<Any?>): Any?
    }
}
