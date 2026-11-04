cmake_minimum_required(VERSION 3.25)
project(flatcam_cpp VERSION 0.4.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version")
endif()

option(FC_BUILD_GUI "Build the Qt6 GUI" ON)
option(FC_BUILD_CLI "Build the command line tool" ON)
option(FC_BUILD_TESTS "Build the test suite" ON)

# Qt6 installed with the online installer is not on the default search path.
if(NOT DEFINED CMAKE_PREFIX_PATH AND EXISTS "$ENV{HOME}/Qt/6.12.0/macos")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.12.0/macos")
endif()

find_path(TCL_INCLUDE_DIR tcl.h HINTS
    /opt/homebrew/opt/tcl-tk/include/tcl-tk
    /usr/local/opt/tcl-tk/include/tcl-tk)
find_library(TCL_LIBRARY NAMES tcl tcl9.1 tcl8.6 HINTS
    /opt/homebrew/opt/tcl-tk/lib
    /usr/local/opt/tcl-tk/lib)

find_package(LibLZMA QUIET)
if(NOT LibLZMA_FOUND)
    find_path(LZMA_INCLUDE_DIR lzma.h HINTS /opt/homebrew/opt/xz/include /usr/local/opt/xz/include)
    find_library(LZMA_LIBRARY NAMES lzma HINTS /opt/homebrew/opt/xz/lib /usr/local/opt/xz/lib)
    if(LZMA_INCLUDE_DIR AND LZMA_LIBRARY)
        add_library(LibLZMA::LibLZMA UNKNOWN IMPORTED)
        set_target_properties(LibLZMA::LibLZMA PROPERTIES
            IMPORTED_LOCATION "${LZMA_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LZMA_INCLUDE_DIR}")
        set(LibLZMA_FOUND TRUE)
    endif()
endif()

find_package(GEOS 3.9 QUIET)
if(GEOS_FOUND)
    set(FC_GEOS_TARGET GEOS::geos_c)
else()
    # Some Linux distributions only ship geos-config / pkg-config.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GEOS_PC REQUIRED IMPORTED_TARGET geos)
    set(FC_GEOS_TARGET PkgConfig::GEOS_PC)
endif()

find_package(ZLIB REQUIRED)

add_library(flatcam_core STATIC
    src/core/units.cpp
    src/core/project.cpp
    src/core/svg.cpp
    src/core/dxf.cpp
    src/core/importers.cpp
    src/core/pdf.cpp
    src/core/geometry.cpp
    src/core/rtree.cpp
    src/core/tools.cpp
    src/core/qr.cpp
    src/core/qrcodegen.cpp
    src/parsers/gerber.cpp
    src/parsers/excellon.cpp
    src/cam/gcode.cpp
    src/cam/isolation.cpp
    src/cam/cutout.cpp
    src/cam/ncc.cpp
    src/cam/paint.cpp
    src/cam/thieving.cpp
)
target_include_directories(flatcam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(flatcam_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(flatcam_core PUBLIC ${FC_GEOS_TARGET})
target_link_libraries(flatcam_core PRIVATE ZLIB::ZLIB)
if(LibLZMA_FOUND)
    target_link_libraries(flatcam_core PRIVATE LibLZMA::LibLZMA)
    target_compile_definitions(flatcam_core PRIVATE FC_HAVE_LZMA=1)
endif()
target_compile_options(flatcam_core PRIVATE -Wall -Wextra -Wpedantic)

if(FC_BUILD_CLI)
    add_executable(flatcam-cli src/cli/main.cpp)
    if(TCL_INCLUDE_DIR AND TCL_LIBRARY)
        target_sources(flatcam-cli PRIVATE src/cli/tcl_embed.cpp)
        target_include_directories(flatcam-cli PRIVATE ${TCL_INCLUDE_DIR})
        target_link_libraries(flatcam-cli PRIVATE ${TCL_LIBRARY})
        target_compile_definitions(flatcam-cli PRIVATE FC_HAVE_TCL=1)
        message(STATUS "Tcl embedding enabled: ${TCL_LIBRARY}")
    else()
        target_sources(flatcam-cli PRIVATE src/cli/tcl.cpp)
        message(STATUS "Tcl not found: using the built-in mini shell")
    endif()
    target_link_libraries(flatcam-cli PRIVATE flatcam_core)
endif()

if(FC_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Qml Svg PrintSupport Sql)
    qt_standard_project_setup()

    add_library(flatcam_script STATIC src/script/ScriptApi.cpp)
    set_target_properties(flatcam_script PROPERTIES AUTOMOC ON)
    target_include_directories(flatcam_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/script)
    target_link_libraries(flatcam_script PUBLIC flatcam_core Qt6::Core Qt6::Qml)

    option(FC_BUILD_JS "Build the Qt JavaScript runner" ON)
    if(FC_BUILD_JS)
        add_executable(flatcam-js src/script/main.cpp)
        target_link_libraries(flatcam-js PRIVATE flatcam_script)
    endif()

    qt_add_executable(flatcam-gui
        src/gui/main.cpp
        src/gui/MainWindow.cpp
        src/gui/Theme.cpp
        src/gui/Theme.h
        src/gui/Canvas.cpp
        src/gui/View3D.cpp
        src/gui/GCodeEditor.cpp
        src/gui/TextGeometry.cpp
        src/gui/TextGeometry.h
        src/gui/PdfPages.h
        assets/icons.qrc
    )
    if(APPLE)
        enable_language(OBJCXX)
        target_sources(flatcam-gui PRIVATE src/gui/PdfPages.mm)
        target_link_libraries(flatcam-gui PRIVATE "-framework PDFKit" "-framework AppKit")
    endif()
    target_link_libraries(flatcam-gui PRIVATE flatcam_core flatcam_script Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::PrintSupport Qt6::Sql)
    if(TCL_INCLUDE_DIR AND TCL_LIBRARY)
        target_include_directories(flatcam-gui PRIVATE ${TCL_INCLUDE_DIR})
        target_link_libraries(flatcam-gui PRIVATE ${TCL_LIBRARY})
        target_compile_definitions(flatcam-gui PRIVATE FC_HAVE_TCL=1)
    endif()
    set_target_properties(flatcam-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.flatcam.cpp"
        MACOSX_BUNDLE_BUNDLE_NAME "FlatCAM C++"
    )
endif()

include(GNUInstallDirs)
install(TARGETS flatcam_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(FC_BUILD_CLI)
    install(TARGETS flatcam-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(FC_BUILD_GUI)
    install(TARGETS flatcam-gui BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(TARGET flatcam-js)
        install(TARGETS flatcam-js RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
install(DIRECTORY examples DESTINATION ${CMAKE_INSTALL_DATADIR}/flatcam)

set(CPACK_PACKAGE_NAME "flatcam-cpp")
set(CPACK_PACKAGE_VENDOR "FlatCAM C++")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)

if(FC_BUILD_TESTS)
    include(CTest)
    find_package(GTest CONFIG REQUIRED)
    enable_testing()
    add_executable(flatcam_tests
        tests/test_geometry.cpp
        tests/test_gerber.cpp
        tests/test_excellon.cpp
        tests/test_gcode.cpp
        tests/test_isolation.cpp
        tests/test_cutout.cpp
        tests/test_ncc.cpp
        tests/test_project.cpp
        tests/test_paint.cpp
        tests/test_thieving.cpp
        tests/test_tools.cpp
        tests/test_qr.cpp
        tests/test_dxf.cpp
        tests/test_importers.cpp
        tests/test_pdf.cpp
    )
    target_link_libraries(flatcam_tests PRIVATE flatcam_core GTest::gtest_main)
    target_compile_definitions(flatcam_tests PRIVATE
        FC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME flatcam_tests COMMAND flatcam_tests)

    if(FC_BUILD_GUI AND TARGET Qt6::Widgets)
        add_executable(flatcam_gui_tests
            tests/test_svgtext.cpp
            src/gui/TextGeometry.cpp
            src/gui/TextGeometry.h
        )
        target_link_libraries(flatcam_gui_tests PRIVATE flatcam_core Qt6::Gui Qt6::Widgets GTest::gtest)
        target_include_directories(flatcam_gui_tests PRIVATE src/gui)
        add_test(NAME flatcam_gui_tests COMMAND flatcam_gui_tests)
        set_tests_properties(flatcam_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
