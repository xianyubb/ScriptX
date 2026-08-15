# The Kotlin backend embeds one JVM in the native process and loads Kotlin's
# JSR-223 engine from the compiler distribution.
find_package(Java REQUIRED COMPONENTS Development Runtime)
find_package(JNI REQUIRED)
find_program(SCRIPTX_KOTLIN_EXECUTABLE NAMES kotlinc kotlinc.bat REQUIRED)

get_filename_component(SCRIPTX_KOTLIN_BIN_DIR "${SCRIPTX_KOTLIN_EXECUTABLE}" DIRECTORY)
get_filename_component(SCRIPTX_KOTLIN_HOME "${SCRIPTX_KOTLIN_BIN_DIR}/.." ABSOLUTE)
set(SCRIPTX_KOTLIN_LIB_DIR "${SCRIPTX_KOTLIN_HOME}/lib")
if (NOT EXISTS "${SCRIPTX_KOTLIN_LIB_DIR}/kotlin-main-kts.jar")
    message(FATAL_ERROR "Kotlin backend requires kotlin-main-kts.jar under "
            "${SCRIPTX_KOTLIN_LIB_DIR}")
endif ()

set(SCRIPTX_KOTLIN_HOST_CLASSES "${CMAKE_CURRENT_BINARY_DIR}/ScriptXKotlinHostClasses")
set(SCRIPTX_KOTLIN_HOST_JAR "${CMAKE_CURRENT_BINARY_DIR}/ScriptXKotlinHost.jar")
file(MAKE_DIRECTORY "${SCRIPTX_KOTLIN_HOST_CLASSES}")
execute_process(
        COMMAND "${Java_JAVAC_EXECUTABLE}" -d "${SCRIPTX_KOTLIN_HOST_CLASSES}"
                "${CMAKE_CURRENT_LIST_DIR}/jvm/ScriptXKotlinHost.java"
        RESULT_VARIABLE SCRIPTX_KOTLIN_HOST_COMPILE_RESULT
        OUTPUT_VARIABLE SCRIPTX_KOTLIN_HOST_COMPILE_OUTPUT
        ERROR_VARIABLE SCRIPTX_KOTLIN_HOST_COMPILE_ERROR)
if (NOT SCRIPTX_KOTLIN_HOST_COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to compile ScriptX Kotlin JVM host:\n"
            "${SCRIPTX_KOTLIN_HOST_COMPILE_OUTPUT}${SCRIPTX_KOTLIN_HOST_COMPILE_ERROR}")
endif ()
execute_process(
        COMMAND "${Java_JAR_EXECUTABLE}" --create --file "${SCRIPTX_KOTLIN_HOST_JAR}"
                -C "${SCRIPTX_KOTLIN_HOST_CLASSES}" .
        RESULT_VARIABLE SCRIPTX_KOTLIN_HOST_JAR_RESULT
        ERROR_VARIABLE SCRIPTX_KOTLIN_HOST_JAR_ERROR)
if (NOT SCRIPTX_KOTLIN_HOST_JAR_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to package ScriptX Kotlin JVM host: "
            "${SCRIPTX_KOTLIN_HOST_JAR_ERROR}")
endif ()

# The token is expanded to the platform classpath separator by the native runtime.
set(SCRIPTX_KOTLIN_CLASSPATH_SEPARATOR "__SCRIPTX_KOTLIN_PATHSEP__")
file(GLOB SCRIPTX_KOTLIN_RUNTIME_JARS "${SCRIPTX_KOTLIN_LIB_DIR}/*.jar")
set(SCRIPTX_KOTLIN_CLASSPATH "${SCRIPTX_KOTLIN_HOST_JAR}")
foreach (SCRIPTX_KOTLIN_RUNTIME_JAR IN LISTS SCRIPTX_KOTLIN_RUNTIME_JARS)
    get_filename_component(SCRIPTX_KOTLIN_RUNTIME_JAR_NAME
            "${SCRIPTX_KOTLIN_RUNTIME_JAR}" NAME)
    # Compiler plugins are discovered through service-loader metadata and can
    # alter scripting compilation (Compose, ABI generation, annotation processing).
    if (SCRIPTX_KOTLIN_RUNTIME_JAR_NAME MATCHES
            "(compiler-plugin|jvm-abi-gen|annotation-processing|compose-compiler|kapt|parcelize|lombok|noarg|power-assert|sam-with|serialization-compiler|mutability|allopen|assignment)")
        continue()
    endif ()
    string(APPEND SCRIPTX_KOTLIN_CLASSPATH
            "${SCRIPTX_KOTLIN_CLASSPATH_SEPARATOR}${SCRIPTX_KOTLIN_RUNTIME_JAR}")
endforeach ()
string(REPLACE "\\" "/" SCRIPTX_KOTLIN_CLASSPATH "${SCRIPTX_KOTLIN_CLASSPATH}")
string(REPLACE "\"" "\\\"" SCRIPTX_KOTLIN_CLASSPATH_DEFINE "${SCRIPTX_KOTLIN_CLASSPATH}")

target_sources(ScriptX PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/KotlinEngine.cc
        ${CMAKE_CURRENT_LIST_DIR}/KotlinEngine.h
        ${CMAKE_CURRENT_LIST_DIR}/KotlinException.cc
        ${CMAKE_CURRENT_LIST_DIR}/KotlinNative.cc
        ${CMAKE_CURRENT_LIST_DIR}/KotlinNative.hpp
        ${CMAKE_CURRENT_LIST_DIR}/KotlinReference.hpp
        ${CMAKE_CURRENT_LIST_DIR}/KotlinRuntime.cc
        ${CMAKE_CURRENT_LIST_DIR}/KotlinRuntime.h
        ${CMAKE_CURRENT_LIST_DIR}/KotlinUtils.cc
        ${CMAKE_CURRENT_LIST_DIR}/KotlinValue.cc)
target_include_directories(ScriptX PRIVATE ${JNI_INCLUDE_DIRS})
target_link_libraries(ScriptX PUBLIC ${JNI_LIBRARIES})
target_compile_definitions(ScriptX PRIVATE
        SCRIPTX_KOTLIN_CLASSPATH="${SCRIPTX_KOTLIN_CLASSPATH_DEFINE}")
