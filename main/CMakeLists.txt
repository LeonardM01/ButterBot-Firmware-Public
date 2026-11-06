file(GLOB_RECURSE LIBS "lib/*/src/**.cpp" "lib/*/src/**.c")

file(GLOB_RECURSE SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/**.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/**.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/**.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/**.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

idf_component_register(SRCS ${SOURCES} ${LIBS} INCLUDE_DIRS "src")

idf_build_get_property(BASEDIR PROJECT_DIR)

# Measurement build (#20, docs/measurement-build.md): `#define BB_DEBUG_ASK 1` in secrets.h compiles in the debug
# ask turn (BB_DEBUG_ASK_BUILD) and adds its canned question to the storage SPIFFS image. Only that one line is read;
# the root CMakeLists.txt re-runs this when secrets.h changes.
set(BB_DEBUG_ASK 0)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/secrets.h" BB_DEBUG_ASK_LINE REGEX "^[ \t]*#[ \t]*define[ \t]+BB_DEBUG_ASK[ \t]")
if(BB_DEBUG_ASK_LINE)
    if(BB_DEBUG_ASK_LINE MATCHES "BB_DEBUG_ASK[ \t]+([01])([ \t]|/|$)")
        set(BB_DEBUG_ASK ${CMAKE_MATCH_1})
    else()
        message(FATAL_ERROR "main/secrets.h: BB_DEBUG_ASK must be defined as 0 or 1")
    endif()
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE BB_DEBUG_ASK_BUILD=${BB_DEBUG_ASK})

if(BB_DEBUG_ASK)
    message(WARNING "BB_DEBUG_ASK is 1: measurement build with the debug ask turn. Never ship this.")
    # The canned question is a local, gitignored file (no audio in git); see docs/measurement-build.md step 1.
    if(NOT EXISTS "${BASEDIR}/debug/question.wav")
        message(FATAL_ERROR "BB_DEBUG_ASK is 1 but debug/question.wav is missing. Copy a 16 kHz mono s16 WAV "
                "there, e.g. butterbot-api/internal/ask/testdata/question.wav from the parent repo.")
    endif()
    # Stage spiffs/ plus the canned question in the build dir; release images stay as they are.
    set(DEBUG_SPIFFS_DIR "${CMAKE_BINARY_DIR}/spiffs_debug_ask")
    add_custom_target(debug_ask_spiffs_stage
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${DEBUG_SPIFFS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${BASEDIR}/spiffs" "${DEBUG_SPIFFS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${BASEDIR}/debug/question.wav" "${DEBUG_SPIFFS_DIR}/debug/question.wav"
            VERBATIM)
    spiffs_create_partition_image(storage "${DEBUG_SPIFFS_DIR}" FLASH_IN_PROJECT)
    add_dependencies(spiffs_storage_bin debug_ask_spiffs_stage)
else()
    spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
endif()

esptool_py_flash_to_partition(flash objdet ${BASEDIR}/models/objdet.tflite)
