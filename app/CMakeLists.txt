find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia)
set(CMAKE_AUTOMOC ON)

qt_add_executable(loggx-studio
  src/main.cpp
  src/AppController.cpp src/AppController.h
  src/Items.cpp src/Items.h
  src/Workers.cpp src/Workers.h
  src/MediaCache.cpp src/MediaCache.h
  src/TitleRenderer.cpp src/TitleRenderer.h
  src/IconItem.cpp src/IconItem.h
  src/AppControllerMusic.cpp
  src/AppControllerTools.cpp
  src/MicRecorder.cpp src/MicRecorder.h
  src/MidiInput.cpp src/MidiInput.h
)

qt_add_resources(loggx-studio "qml"
  PREFIX "/"
  FILES
    qml/Main.qml
    qml/EditPage.qml
    qml/MediaPage.qml
    qml/ExportPage.qml
    qml/Inspector.qml
    qml/ValueSlider.qml
    qml/ColorPage.qml
    qml/MusicPage.qml
    qml/MixerStrip.qml
    qml/Section.qml
    qml/StartPage.qml
    qml/LicenseDialog.qml
    qml/IconButton.qml
    qml/Divider.qml
    qml/LiveKeyboard.qml
    qml/ProjectFormatForm.qml
    qml/ProjectSettingsDialog.qml
    qml/FileDropArea.qml
    qml/PreviewBar.qml
    qml/LoggXStyle/Button.qml
    qml/LoggXStyle/CheckBox.qml
    qml/LoggXStyle/ComboBox.qml
    qml/LoggXStyle/Dialog.qml
    qml/LoggXStyle/ItemDelegate.qml
    qml/LoggXStyle/Label.qml
    qml/LoggXStyle/Menu.qml
    qml/LoggXStyle/MenuItem.qml
    qml/LoggXStyle/Popup.qml
    qml/LoggXStyle/ProgressBar.qml
    qml/LoggXStyle/ScrollBar.qml
    qml/LoggXStyle/Slider.qml
    qml/LoggXStyle/SpinBox.qml
    qml/LoggXStyle/Switch.qml
    qml/LoggXStyle/TabBar.qml
    qml/LoggXStyle/TabButton.qml
    qml/LoggXStyle/TextArea.qml
    qml/LoggXStyle/TextField.qml
    qml/LoggXStyle/Theme.qml
    qml/LoggXStyle/ToolButton.qml
    qml/LoggXStyle/ToolTip.qml
    qml/LoggXStyle/qmldir
)
qt_add_resources(loggx-studio "icons"
  PREFIX "/"
  FILES resources/loggx-256.png
)

if(WIN32)
  configure_file(resources/loggx.rc.in ${CMAKE_CURRENT_BINARY_DIR}/loggx.rc @ONLY)
  target_sources(loggx-studio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/loggx.rc)
endif()

# MIDI-Keyboards (optional): RtMidi über sein CMake-Paket (vcpkg), pkg-config
# (Linux/macOS) oder direkt gesucht
add_library(loggx_rtmidi INTERFACE)
set(LOGGX_HAVE_RTMIDI OFF)
find_package(RtMidi CONFIG QUIET)
if(NOT TARGET RtMidi::rtmidi AND PkgConfig_FOUND AND NOT WIN32)
  pkg_check_modules(LOGGX_RTMIDI IMPORTED_TARGET rtmidi)
endif()
if(TARGET RtMidi::rtmidi)
  target_link_libraries(loggx_rtmidi INTERFACE RtMidi::rtmidi)
  set(LOGGX_HAVE_RTMIDI ON)
elseif(TARGET PkgConfig::LOGGX_RTMIDI)
  target_link_libraries(loggx_rtmidi INTERFACE PkgConfig::LOGGX_RTMIDI)
  set(LOGGX_HAVE_RTMIDI ON)
else()
  find_path(LOGGX_RTMIDI_INCLUDE RtMidi.h PATH_SUFFIXES rtmidi)
  find_library(LOGGX_RTMIDI_LIB NAMES rtmidi)
  if(LOGGX_RTMIDI_INCLUDE AND LOGGX_RTMIDI_LIB)
    target_include_directories(loggx_rtmidi INTERFACE ${LOGGX_RTMIDI_INCLUDE})
    target_link_libraries(loggx_rtmidi INTERFACE ${LOGGX_RTMIDI_LIB})
    if(WIN32)
      target_link_libraries(loggx_rtmidi INTERFACE winmm)
    endif()
    set(LOGGX_HAVE_RTMIDI ON)
  endif()
endif()
if(LOGGX_HAVE_RTMIDI)
  target_compile_definitions(loggx_rtmidi INTERFACE LOGGX_WITH_RTMIDI)
  message(STATUS "LoggX: MIDI-Keyboards aktiv (RtMidi)")
else()
  message(STATUS "LoggX: RtMidi nicht gefunden - ohne MIDI-Keyboards")
endif()
target_compile_definitions(loggx_rtmidi INTERFACE LOGGX_LICENSE_PUBKEY="${LOGGX_LICENSE_PUBKEY}")

target_link_libraries(loggx-studio PRIVATE loggx_rtmidi
  loggx_engine
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)

set_target_properties(loggx-studio PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME "LoggX Studio"
  MACOSX_BUNDLE_GUI_IDENTIFIER "de.loggx.studio"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  OUTPUT_NAME "LoggX Studio")
target_compile_definitions(loggx-studio PRIVATE LOGGX_VERSION="${PROJECT_VERSION}")

if(LOGGX_BUILD_TESTS)
  qt_add_executable(test_app
    ../tests/test_app.cpp
    src/AppController.cpp src/AppController.h
    src/Workers.cpp src/Workers.h
    src/MediaCache.cpp src/MediaCache.h
    src/TitleRenderer.cpp src/TitleRenderer.h
    src/AppControllerMusic.cpp
    src/AppControllerTools.cpp
    src/MicRecorder.cpp src/MicRecorder.h
    src/MidiInput.cpp src/MidiInput.h)
  target_include_directories(test_app PRIVATE src ../tests)
  target_link_libraries(test_app PRIVATE loggx_engine loggx_rtmidi Qt6::Core Qt6::Gui Qt6::Multimedia)
  add_test(NAME app COMMAND test_app)
endif()

# Kommandozeilen-Export mit Titeln (Schrift über Qt, ohne Fenster)
qt_add_executable(loggx-render-qt ../tools/loggx_render.cpp src/TitleRenderer.cpp src/TitleRenderer.h)
target_compile_definitions(loggx-render-qt PRIVATE LOGGX_WITH_QT_TITLES)
target_include_directories(loggx-render-qt PRIVATE src)
target_link_libraries(loggx-render-qt PRIVATE loggx_engine Qt6::Gui)
set_target_properties(loggx-render-qt PROPERTIES OUTPUT_NAME loggx-render RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
