package org.legacy.scriptengine.kotlin.runtime

/**
 * Generic dispatcher used by API layers outside ScriptX (for example LSE).
 * ScriptX itself does not define LSE classes or functions.
 */
object ScriptXKotlinRuntime {
    interface Dispatcher {
        fun call(name: String, args: Array<Any?>): Any?
        fun callInstance(
            className: String,
            method: String,
            receiver: Any?,
            args: Array<Any?>
        ): Any?
        fun construct(className: String, args: Array<Any?>): Any?
        fun nativeClassName(value: Any?): String?
    }

    private val current = ThreadLocal<Dispatcher?>()

    fun bind(dispatcher: Dispatcher) {
        current.set(dispatcher)
    }

    fun clear() {
        current.remove()
    }

    fun capture(): Dispatcher =
        requireDispatcher()

    fun <T> withDispatcher(dispatcher: Dispatcher, action: () -> T): T {
        val previous = current.get()
        current.set(dispatcher)
        return try {
            action()
        } finally {
            if (previous == null) current.remove() else current.set(previous)
        }
    }

    @Suppress("UNCHECKED_CAST")
    fun call(name: String, vararg args: Any?): Any? =
        requireDispatcher().call(name, args as Array<Any?>)

    @Suppress("UNCHECKED_CAST")
    fun callInstance(
        className: String,
        method: String,
        receiver: Any?,
        vararg args: Any?
    ): Any? = requireDispatcher().callInstance(
        className,
        method,
        receiver,
        args as Array<Any?>
    )

    @Suppress("UNCHECKED_CAST")
    fun construct(className: String, vararg args: Any?): Any? =
        requireDispatcher().construct(className, args as Array<Any?>)

    fun nativeClassName(value: Any?): String? =
        requireDispatcher().nativeClassName(value)

    private fun requireDispatcher(): Dispatcher =
        current.get() ?: error("LSE Kotlin API is used outside an active plugin callback")
}
