add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

local kotlin_jvm_linkdir = nil

if is_config("backend", "Lua") then
    add_requires("lua v5.5.0", {configs={shared=true}})

elseif is_config("backend", "QuickJs") then
    add_requires("quickjs-ng v0.13.0", {configs={shared=true, libc=true}})

elseif is_config("backend", "Python") then
    add_requires("python 3.12.10", {configs={shared=true}})

elseif is_config("backend", "Kotlin") then
    -- Kotlin embeds a JVM. xmake has no built-in Kotlin toolchain, so discover the
    -- same JDK/Kotlin distribution used by CMake; provide the host classpath below.
    local java_home = os.getenv("JAVA_HOME")
    local kotlin_home = os.getenv("KOTLIN_HOME")
    -- On Windows, JAVA_HOME/KOTLIN_HOME are the portable configuration. Keep
    -- the common local installation locations as a convenience fallback.
    if not java_home and is_host("windows") and os.isdir("C:/Program Files/Zulu/zulu-21") then
        java_home = "C:/Program Files/Zulu/zulu-21"
    end
    if not kotlin_home and is_host("windows") and os.isdir("D:/kotlinc") then
        kotlin_home = "D:/kotlinc"
    end
    if not java_home or not kotlin_home then
        raise("Kotlin backend requires JAVA_HOME/KOTLIN_HOME or java/kotlinc in PATH")
    end
    -- Set SCRIPTX_KOTLIN_CLASSPATH to the host jar plus filtered Kotlin runtime
    -- jars. CMake performs this discovery and host-jar build automatically.
    local classpath = os.getenv("SCRIPTX_KOTLIN_CLASSPATH")
    if classpath then add_defines("SCRIPTX_KOTLIN_CLASSPATH=\"" .. classpath .. "\"") end
    add_includedirs(path.join(java_home, "include"))
    if is_host("windows") then
        add_includedirs(path.join(java_home, "include", "win32"))
        kotlin_jvm_linkdir = path.join(java_home, "lib")
    elseif is_host("linux") then
        add_includedirs(path.join(java_home, "include", "linux"))
        kotlin_jvm_linkdir = path.join(java_home, "lib", "server")
    elseif is_host("macosx") then
        add_includedirs(path.join(java_home, "include", "darwin"))
        kotlin_jvm_linkdir = path.join(java_home, "lib", "server")
    end

elseif is_config("backend", "V8") then
    add_requires("node v22.12.0", {configs={shared=true}})
end

option("backend")
    set_default("Lua")
    set_values("Lua", "QuickJs", "Python", "Kotlin", "V8")

target("ScriptX")
    add_files(
        "src/**.cc"
    )
    add_headerfiles(
        "(**.h)",
        "(**.hpp)"
    )
    add_includedirs(
        "src/include/"
    )
    set_kind("static")
    set_languages("cxx20")

    if is_config("backend", "Lua") then
        add_defines(
            "SCRIPTX_BACKEND_LUA",
            "SCRIPTX_BACKEND_TRAIT_PREFIX=../backend/Lua/trait/Trait"
        )
        add_files(
            "backend/Lua/**.cc"
        )
        add_packages(
            "lua"
        )

    elseif is_config("backend", "QuickJs") then
        add_defines(
            "SCRIPTX_BACKEND_QUICKJS",
            "SCRIPTX_BACKEND_TRAIT_PREFIX=../backend/QuickJs/trait/Trait"
        )
        add_files(
            "backend/QuickJs/**.cc"
        )
        add_packages(
            "quickjs-ng"
        )

    elseif is_config("backend", "Python") then
        add_defines(
            "SCRIPTX_BACKEND_PYTHON",
            "SCRIPTX_BACKEND_TRAIT_PREFIX=../backend/Python/trait/Trait"
        )
        add_files(
            "backend/Python/**.cc",
            "backend/Python/**.c"
        )
        add_packages(
            "python"
        )

    elseif is_config("backend", "V8") then
        add_defines(
            "SCRIPTX_BACKEND_V8",
            "SCRIPTX_BACKEND_TRAIT_PREFIX=../backend/V8/trait/Trait"
        )
        add_files(
            "backend/V8/**.cc"
        )
        add_packages(
            "node"
        )

    elseif is_config("backend", "Kotlin") then
        add_defines(
            "SCRIPTX_BACKEND_TRAIT_PREFIX=../backend/Kotlin/trait/Trait"
        )
        add_files(
            "backend/Kotlin/**.cc"
        )
        -- ScriptX is static. Propagate the JVM import library to targets
        -- linking ScriptX, otherwise JNI_CreateJavaVM remains unresolved.
        add_linkdirs(kotlin_jvm_linkdir, {public = true})
        add_links("jvm", {public = true})

    end
