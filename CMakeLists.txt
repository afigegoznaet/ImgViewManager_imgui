cmake_minimum_required(VERSION 3.24)
project(imgui_ivm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IVM_ENABLE_SANITIZERS "Enable address/undefined behavior sanitizers for crash diagnosis" OFF)
if(IVM_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "IVM_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(Freetype REQUIRED)
find_package(PNG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(ICU REQUIRED COMPONENTS i18n uc)
find_path(TURBOJPEG_INCLUDE_DIR NAMES turbojpeg.h REQUIRED)
find_library(TURBOJPEG_LIBRARY NAMES turbojpeg REQUIRED)

find_package(imgui CONFIG REQUIRED)

add_library(thumbnail_image STATIC src/ThumbnailImage.cpp src/ThumbnailImage.hpp src/ThumbnailDiskCache.cpp)
target_include_directories(thumbnail_image PUBLIC src PRIVATE ${TURBOJPEG_INCLUDE_DIR})
target_link_libraries(thumbnail_image PRIVATE Qt6::Core Qt6::Gui PNG::PNG ${TURBOJPEG_LIBRARY})

add_executable(imgui_ivm
    src/ImageSorting.cpp
    src/ContextActions.cpp
    src/ContextActions.hpp
    src/ThumbnailCache.cpp
    src/ThumbnailCache.hpp
    src/AppState.cpp
    src/AppState.hpp
    src/DirectoryWatcher.cpp
    src/DirectoryWatcher.hpp
    third_party/imgui_freetype/imgui_freetype.cpp
    src/main.cpp
    src/FolderBrowser.cpp
    src/FolderBrowser.hpp
    src/ImageFolderFilter.cpp
    src/ImageFolderFilter.hpp
    src/TreeUpdates.hpp
    src/DirectoryScanner.cpp
    src/DirectoryScanner.hpp
)
target_include_directories(imgui_ivm PRIVATE third_party/imgui_freetype)
target_link_libraries(imgui_ivm PRIVATE
    thumbnail_image ICU::i18n ICU::uc
    imgui::imgui
    SDL3::SDL3
    OpenGL::GL
    Threads::Threads
    Fontconfig::Fontconfig
    Freetype::Freetype
)

# Install the viewer and Linux desktop integration, matching the Qt project.
include(GNUInstallDirs)
install(TARGETS imgui_ivm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES resources/imgui_ivm.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/imgui_ivm.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tests src/ImageSorting.cpp tests/TreeUpdateTest.cpp tests/SessionTest.cpp tests/ThumbnailImageTest.cpp tests/DirectoryScannerTest.cpp src/DirectoryScanner.cpp src/ImageFolderFilter.cpp src/AppState.cpp src/DirectoryWatcher.cpp src/ContextActions.cpp tests/ContextActionsTest.cpp)
    target_include_directories(tests PRIVATE src ${TURBOJPEG_INCLUDE_DIR})
    target_link_libraries(tests PRIVATE Qt6::Core Qt6::Gui ICU::i18n ICU::uc Threads::Threads thumbnail_image PNG::PNG ${TURBOJPEG_LIBRARY})
    add_test(NAME directory_scan_filter COMMAND tests)
endif()
