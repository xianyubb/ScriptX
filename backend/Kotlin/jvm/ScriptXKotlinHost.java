import javax.script.ScriptEngine;
import javax.script.ScriptEngineManager;
import javax.script.ScriptEngineFactory;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.ArrayList;
import java.util.List;
import java.util.LinkedHashMap;

/** Small JVM-side façade used by the native Kotlin backend. */
public final class ScriptXKotlinHost implements AutoCloseable {
    private final ScriptEngine engine;
    private final List<String> preludes = new ArrayList<>();
    private volatile Thread engineThread;
    private final ExecutorService executor = Executors.newSingleThreadExecutor(task -> {
        Thread thread = new Thread(task, "ScriptX-Kotlin");
        thread.setDaemon(true);
        engineThread = thread;
        return thread;
    });

    public ScriptXKotlinHost() {
        engine = onEngineThread(ScriptXKotlinHost::createEngine);
    }

    private static ScriptEngine createEngine() {
        ScriptEngine candidate;
        try {
            Class<?> factoryType = Class.forName(
                    "org.jetbrains.kotlin.mainKts.jsr223.KotlinJsr223MainKtsScriptEngineFactory");
            candidate = ((ScriptEngineFactory) factoryType.getDeclaredConstructor().newInstance())
                    .getScriptEngine();
        } catch (ReflectiveOperationException unavailableMainKtsFactory) {
            ScriptEngineManager manager = new ScriptEngineManager();
            candidate = manager.getEngineByName("kotlin");
            if (candidate == null) candidate = manager.getEngineByExtension("kts");
        }
        if (candidate == null) {
            throw new IllegalStateException(
                    "Kotlin JSR-223 engine is unavailable; kotlin-main-kts.jar is required");
        }
        return candidate;
    }

    private <T> T onEngineThread(Callable<T> action) {
        if (Thread.currentThread() == engineThread) {
            try {
                return action.call();
            } catch (RuntimeException | Error exception) {
                throw exception;
            } catch (Exception exception) {
                throw new IllegalStateException(exception);
            }
        }
        try {
            return executor.submit(action).get();
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while waiting for Kotlin", exception);
        } catch (ExecutionException exception) {
            Throwable cause = exception.getCause();
            if (cause instanceof RuntimeException) throw (RuntimeException) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw new IllegalStateException(cause);
        }
    }

    public Object eval(String source, String sourceFile) {
        return onEngineThread(() -> engine.eval(buildSource(source)));
    }

    private String buildSource(String source) {
        StringBuilder result = new StringBuilder();
        for (String prelude : preludes) result.append(prelude).append('\n');
        return result.append(source).toString();
    }

    public void addPrelude(String source) {
        onEngineThread(() -> {
            preludes.add(source);
            return null;
        });
    }

    public Object newNativeInstance(long pointer, long classId) {
        return new NativeInstance(pointer, classId);
    }

    public Object get(String name) {
        return onEngineThread(() -> engine.get(name));
    }

    public void set(String name, Object value) {
        onEngineThread(() -> {
            engine.put(name, value);
            return null;
        });
    }

    @Override
    public void close() {
        executor.shutdownNow();
    }

    public String version() {
        Package p = ScriptXKotlinHost.class.getPackage();
        return p == null ? "Kotlin/JVM (embedded JSR-223)" :
                "Kotlin/JVM (embedded JSR-223) " + p.getImplementationVersion();
    }

    /** Kotlin's invoke convention makes this Java object callable as fn(arg1, arg2). */
    public static final class NativeFunction {
        private final long callback;

        public NativeFunction(long callback) {
            this.callback = callback;
        }

        public Object invoke(Object... args) {
            return nativeInvoke(callback, args);
        }
    }

    public static final class NativeConstructor {
        private final long callback;
        private final long classId;

        public NativeConstructor(long callback, long classId) {
            this.callback = callback;
            this.classId = classId;
        }

        public Object invoke(Object... args) {
            return nativeConstruct(callback, classId, args);
        }
    }

    /** Opaque native instance holder. It is a Map so ScriptX sees it as an Object. */
    public static final class NativeInstance extends LinkedHashMap<String, Object> {
        private long pointer;
        private final long classId;

        public NativeInstance(long pointer, long classId) {
            this.pointer = pointer;
            this.classId = classId;
        }

        public long pointer() { return pointer; }
        public long classId() { return classId; }
        public void setPointer(long pointer) { this.pointer = pointer; }
    }

    private static native Object nativeInvoke(long callback, Object[] args);
    private static native Object nativeConstruct(long callback, long classId, Object[] args);
}
