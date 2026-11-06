# Wraps ../../../src (the actual asmparser2 library -- the same source
# tree the Arduino library.properties/library.json at the repo root
# point at) as a plain ESP-IDF component, so every project under
# exampleESPIDF/ can pull it in via EXTRA_COMPONENT_DIRS without a copy.
# Nothing in src/ is ESP-IDF-specific by itself -- script_executable.h's
# executeAsTask()/asm_execute.cpp's exec-memory allocation already talk
# to FreeRTOS/esp_heap_caps.h/xtensa/hal.h directly (see their own
# comments), guarded by `#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)`.
# Arduino's core defines that ESP32 macro itself; a plain ESP-IDF project
# doesn't, so it's defined explicitly below -- this library only ever
# targets Xtensa ESP32/S2/S3 (the assembler encodes real Xtensa
# instructions), so defining it unconditionally for this component is
# always correct here, never a guess.
set(ASMPARSER2_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../src")
file(GLOB ASMPARSER2_SRCS "${ASMPARSER2_SRC_DIR}/*.cpp")

idf_component_register(
    SRCS ${ASMPARSER2_SRCS}
    INCLUDE_DIRS "${ASMPARSER2_SRC_DIR}"
    REQUIRES freertos heap esp_timer xtensa
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ESP32=1)

# ESP-IDF's default build flags treat -Wformat/-Wmisleading-indentation as
# errors; this source tree's printf()-family calls were written against
# Arduino-ESP32's newlib (where uint32_t is `unsigned int`), while plain
# ESP-IDF's newlib for this toolchain has it as `long unsigned int` --
# same 32 bits, different type as far as -Wformat is concerned. Neither a
# real bug nor anything examples should be silently "fixing" as a side
# effect, so it's scoped off for just this component rather than edited
# out of the library source.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
    -Wno-error=misleading-indentation
)
