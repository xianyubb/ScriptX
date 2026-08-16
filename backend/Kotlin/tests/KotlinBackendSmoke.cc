#include <ScriptX/ScriptX.h>

#include <cstdint>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: kotlin_backend_smoke <compiled-jar> <kotlin-source>\n";
    return 64;
  }

  try {
    script::UniqueEnginePtr engine(new script::ScriptEngineImpl());
    script::EngineScope scope(engine.get());

    bool rejectedSource = false;
    try {
      engine->loadFile(argv[2]);
    } catch (const script::Exception&) {
      rejectedSource = true;
    }
    if (!rejectedSource) {
      std::cerr << "Kotlin backend accepted source input in JAR-only mode\n";
      return 1;
    }

    engine->loadFile(argv[1]);

    auto nativeMemory = std::shared_ptr<void>(new uint8_t[4]{}, [](void* pointer) {
      delete[] static_cast<uint8_t*>(pointer);
    });
    auto byteBuffer = script::ByteBuffer::newByteBuffer(nativeMemory, 4);
    if (!byteBuffer.isShared() || byteBuffer.getRawBytes() != nativeMemory.get()) {
      std::cerr << "Kotlin direct ByteBuffer is not sharing native memory\n";
      return 2;
    }

    script::Weak<script::ByteBuffer> weak(byteBuffer);
    if (weak.isEmpty() || weak.get().getRawBytes() != nativeMemory.get()) {
      std::cerr << "Kotlin weak reference could not promote a live value\n";
      return 3;
    }

    if (engine->getHeapSize() == 0) {
      std::cerr << "Kotlin JVM heap accounting returned zero\n";
      return 4;
    }
    engine->gc();
    std::cout << "Kotlin ScriptX JAR-only smoke test passed\n";
    return 0;
  } catch (const script::Exception& exception) {
    std::cerr << exception.message() << '\n' << exception.stacktrace() << '\n';
    return 5;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 6;
  }
}
