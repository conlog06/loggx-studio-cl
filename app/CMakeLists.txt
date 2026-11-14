find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia)
set(CMAKE_AUTOMOC ON)

qt_add_executable(loggx-studio
  src/main.cpp
  src/AppController.cpp src/AppController.h
  src/Items.cpp src/Items.h
  src/Workers.cpp src/Workers.h
)

qt_add_resources(loggx-studio "qml"
  PREFIX "/"
  FILES
    qml/Main.qml
    qml/EditPage.qml
    qml/MediaPage.qml
    qml/ExportPage.qml
    qml/Inspector.qml
    qml/PlannedPage.qml
    qml/ValueSlider.qml
)

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
    src/Workers.cpp src/Workers.h)
  target_include_directories(test_app PRIVATE src ../tests)
  target_link_libraries(test_app PRIVATE loggx_engine Qt6::Core Qt6::Gui Qt6::Multimedia)
  add_test(NAME app COMMAND test_app)
endif()
