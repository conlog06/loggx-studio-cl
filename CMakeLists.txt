cmake_minimum_required(VERSION 3.21)
project(LoggXStudio VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LOGGX_BUILD_APP "Qt-Oberfläche bauen" ON)
option(LOGGX_BUILD_TESTS "Tests bauen" ON)

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive-)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- JSON
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# -------------------------------------------------------------- FFmpeg
# Linux/macOS: pkg-config. Windows: vcpkg (find_package(FFMPEG)).
add_library(loggx_ffmpeg INTERFACE)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LOGGX_FFMPEG IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)
endif()
if(TARGET PkgConfig::LOGGX_FFMPEG)
  target_link_libraries(loggx_ffmpeg INTERFACE PkgConfig::LOGGX_FFMPEG)
else()
  find_package(FFMPEG REQUIRED)
  target_include_directories(loggx_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(loggx_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(loggx_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
endif()

# ------------------------------------------- Rubber Band (optional)
# Für Pitch-Shift und Time-Stretch. Ohne die Bibliothek wird gebaut,
# aber Tonhöhe und Tempo von Audio lassen sich dann nicht ändern.
add_library(loggx_rubberband INTERFACE)
set(LOGGX_HAVE_RUBBERBAND OFF)
if(PkgConfig_FOUND)
  pkg_check_modules(LOGGX_RUBBERBAND IMPORTED_TARGET rubberband)
endif()
if(TARGET PkgConfig::LOGGX_RUBBERBAND)
  target_link_libraries(loggx_rubberband INTERFACE PkgConfig::LOGGX_RUBBERBAND)
  set(LOGGX_HAVE_RUBBERBAND ON)
else()
  # Windows/vcpkg: kein pkg-config, daher direkt nach Header und Bibliothek suchen.
  find_path(LOGGX_RUBBERBAND_INCLUDE rubberband/RubberBandStretcher.h)
  find_library(LOGGX_RUBBERBAND_LIB NAMES rubberband rubberband-library rubberband-static)
  if(LOGGX_RUBBERBAND_INCLUDE AND LOGGX_RUBBERBAND_LIB)
    target_include_directories(loggx_rubberband INTERFACE ${LOGGX_RUBBERBAND_INCLUDE})
    target_link_libraries(loggx_rubberband INTERFACE ${LOGGX_RUBBERBAND_LIB})
    set(LOGGX_HAVE_RUBBERBAND ON)
  endif()
endif()

# ----------------------------------------------------------- Bibliotheken
# Monocypher (Ed25519-Signaturen für Lizenzschlüssel), BSD-2/CC0
add_library(monocypher STATIC third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC third_party/monocypher)
set_target_properties(monocypher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(monocypher PRIVATE /W0)
else()
  target_compile_options(monocypher PRIVATE -w)
endif()

add_library(loggx_core STATIC core/src/Project.cpp core/src/Midi.cpp core/src/License.cpp)
target_include_directories(loggx_core PUBLIC core/include)
target_link_libraries(loggx_core PRIVATE nlohmann_json::nlohmann_json monocypher)

# Öffentlicher Lizenzschlüssel (siehe license/README.md). Fehlt er, läuft
# die App als Entwicklerversion.
set(LOGGX_LICENSE_PUBKEY "")
if(EXISTS ${CMAKE_SOURCE_DIR}/license/public_key.txt)
  file(READ ${CMAKE_SOURCE_DIR}/license/public_key.txt LOGGX_LICENSE_PUBKEY)
  string(STRIP "${LOGGX_LICENSE_PUBKEY}" LOGGX_LICENSE_PUBKEY)
endif()

add_library(loggx_media STATIC media/src/Readers.cpp media/src/Exporter.cpp)
target_include_directories(loggx_media PUBLIC media/include)
target_link_libraries(loggx_media PUBLIC loggx_core PRIVATE loggx_ffmpeg)

add_library(loggx_engine STATIC
  engine/src/Compositor.cpp engine/src/AudioMixer.cpp engine/src/Render.cpp
  engine/src/ImageOps.cpp engine/src/Synth.cpp engine/src/AudioFx.cpp engine/src/Beat.cpp
  engine/src/Parallel.cpp engine/src/Spectral.cpp engine/src/AiSeparation.cpp engine/src/Dj.cpp)
target_include_directories(loggx_engine PUBLIC engine/include)
target_link_libraries(loggx_engine PUBLIC loggx_core loggx_media)
if(LOGGX_HAVE_RUBBERBAND)
  target_link_libraries(loggx_engine PRIVATE loggx_rubberband)
  target_compile_definitions(loggx_engine PRIVATE LOGGX_WITH_RUBBERBAND)
  message(STATUS "LoggX: Pitch-Shift/Time-Stretch aktiv (Rubber Band)")
else()
  message(STATUS "LoggX: Rubber Band nicht gefunden - ohne Pitch-Shift/Time-Stretch")
endif()

# KI-Stemtrennung (optional): ONNX Runtime, z. B. das vorgebaute Paket von
# github.com/microsoft/onnxruntime/releases – Ordner mit include/ und lib/.
set(LOGGX_ONNXRUNTIME_DIR "$ENV{ONNXRUNTIME_DIR}" CACHE PATH "Ordner der ONNX Runtime (include/, lib/)")
find_path(LOGGX_ORT_INCLUDE onnxruntime_cxx_api.h
  HINTS "${LOGGX_ONNXRUNTIME_DIR}/include" PATH_SUFFIXES onnxruntime onnxruntime/core/session)
find_library(LOGGX_ORT_LIB NAMES onnxruntime HINTS "${LOGGX_ONNXRUNTIME_DIR}/lib")
if(LOGGX_ORT_INCLUDE AND LOGGX_ORT_LIB)
  target_include_directories(loggx_engine PRIVATE ${LOGGX_ORT_INCLUDE})
  target_link_libraries(loggx_engine PUBLIC ${LOGGX_ORT_LIB})
  target_compile_definitions(loggx_engine PRIVATE LOGGX_HAVE_ONNX)
  get_filename_component(LOGGX_ORT_LIBDIR "${LOGGX_ORT_LIB}" DIRECTORY)
  set(LOGGX_HAVE_ONNX ON)
  # Windows: DLLs der ONNX Runtime neben jede Programmdatei kopieren
  file(GLOB LOGGX_ORT_DLLS "${LOGGX_ORT_LIBDIR}/*.dll")
  message(STATUS "LoggX: KI-Stemtrennung aktiv (ONNX Runtime: ${LOGGX_ORT_LIB})")
else()
  set(LOGGX_HAVE_ONNX OFF)
  message(STATUS "LoggX: ONNX Runtime nicht gefunden - Stimmentrennung nur ohne KI")
endif()

add_executable(loggx-license tools/loggx_license.cpp)
target_link_libraries(loggx-license PRIVATE loggx_core)

add_executable(loggx-render tools/loggx_render.cpp)
target_link_libraries(loggx-render PRIVATE loggx_engine)

if(LOGGX_BUILD_TESTS)
  enable_testing()
endif()

if(LOGGX_BUILD_APP)
  add_subdirectory(app)
endif()

if(LOGGX_BUILD_TESTS)
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE loggx_core)
  add_test(NAME core COMMAND test_core)

  add_executable(test_render tests/test_render.cpp)
  target_link_libraries(test_render PRIVATE loggx_engine)
  add_test(NAME render COMMAND test_render)

  add_executable(test_mvp tests/test_mvp.cpp)
  target_link_libraries(test_mvp PRIVATE loggx_engine)
  target_include_directories(test_mvp PRIVATE engine/src)
  add_test(NAME mvp COMMAND test_mvp)
endif()

# ONNX-Runtime-DLLs (Windows) neben die Programme, die die Engine nutzen
if(WIN32 AND LOGGX_HAVE_ONNX AND LOGGX_ORT_DLLS)
  foreach(t loggx-render test_core test_render test_mvp loggx-studio test_app)
    if(TARGET ${t})
      add_custom_command(TARGET ${t} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LOGGX_ORT_DLLS} $<TARGET_FILE_DIR:${t}>)
    endif()
  endforeach()
endif()
