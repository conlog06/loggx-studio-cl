find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia)
set(CMAKE_AUTOMOC ON)

qt_add_executable(loggx-studio
  src/main.cpp
  src/AppController.cpp src/AppController.h
  src/Items.cpp src/Items.h
  src/Workers.cpp src/Workers.h
  src/MediaCache.cpp src/MediaCache.h
  src/TitleRenderer.cpp src/TitleRenderer.h
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
)
qt_add_resources(loggx-studio "icons"
  PREFIX "/"
  FILES resources/loggx-256.png
)

if(WIN32)
  configure_file(resources/loggx.rc.in ${CMAKE_CURRENT_BINARY_DIR}/loggx.rc @ONLY)
  target_sources(loggx-studio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/loggx.rc)
endif()

target_link_libraries(loggx-studio PRIVATE
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
    src/TitleRenderer.cpp src/TitleRenderer.h)
  target_include_directories(test_app PRIVATE src ../tests)
  target_link_libraries(test_app PRIVATE loggx_engine Qt6::Core Qt6::Gui Qt6::Multimedia)
  add_test(NAME app COMMAND test_app)
endif()

# Kommandozeilen-Export mit Titeln (Schrift über Qt, ohne Fenster)
qt_add_executable(loggx-render-qt ../tools/loggx_render.cpp src/TitleRenderer.cpp src/TitleRenderer.h)
target_compile_definitions(loggx-render-qt PRIVATE LOGGX_WITH_QT_TITLES)
target_include_directories(loggx-render-qt PRIVATE src)
target_link_libraries(loggx-render-qt PRIVATE loggx_engine Qt6::Gui)
set_target_properties(loggx-render-qt PROPERTIES OUTPUT_NAME loggx-render RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
