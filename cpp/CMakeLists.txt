# SJJ-COM-Tool (C++ / Qt6) 串口调试工具
# 功能与原 PySide6 版完全一致。
# 依赖：Qt 6.5+（Widgets / SerialPort / Network / Core5Compat 组件）
cmake_minimum_required(VERSION 3.16)

project(SJJ-COM-Tool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# MSVC 使用 UTF-8 源码字符集（源码含中文注释/字符串）
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network Core5Compat)

add_executable(SJJ-COM-Tool
    resources.qrc
    src/main.cpp
    src/themes.h
    src/themes.cpp
    src/utils.h
    src/utils.cpp
    src/combobox.h
    src/combobox.cpp
    src/themechkbox.h
    src/themechkbox.cpp
    src/titlebar.h
    src/titlebar.cpp
    src/updater.h
    src/updater.cpp
    src/serialtool.h
    src/serialtool.cpp
)

target_link_libraries(SJJ-COM-Tool PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Network
    Qt6::Core5Compat
)

# ===== 静态链接 Qt（Qt 官方无预编译静态 MinGW 包，需自行编译） =====
# 检测 Qt 是否为静态库；静态模式导入平台/样式插件并定义 SJJ_STATIC_BUILD
get_target_property(_qt_core_type Qt6::Core TYPE)
if(_qt_core_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(SJJ-COM-Tool PRIVATE SJJ_STATIC_BUILD)
    target_link_libraries(SJJ-COM-Tool PRIVATE
        Qt6::QWindowsIntegrationPlugin
        Qt6::QModernWindowsStylePlugin
    )
    # 全静态链接：libgcc/libstdc++/winpthread 均静态，exe 只依赖系统 DLL
    target_link_options(SJJ-COM-Tool PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# 版本号注入（可被 CI 覆盖，例如：-DAPP_VERSION=2.0.3）
if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "dev")
endif()
target_compile_definitions(SJJ-COM-Tool PRIVATE APP_VERSION="${APP_VERSION}")

# Windows 版本资源（右键 exe 属性→详细信息可见文件版本/产品版本）
# 从 APP_VERSION 拆出 主.次.修订，生成 VERSIONINFO 资源
if(WIN32)
    string(REPLACE "." ";" _ver_parts "${APP_VERSION}")
    list(GET _ver_parts 0 APP_VERSION_MAJOR)
    list(GET _ver_parts 1 APP_VERSION_MINOR)
    list(GET _ver_parts 2 APP_VERSION_PATCH)
    configure_file(src/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(SJJ-COM-Tool PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

if(WIN32)
    # Qt6::Core 会根据 WIN32_EXECUTABLE 属性自动链接 Qt6::EntryPointPrivate
    target_link_libraries(SJJ-COM-Tool PRIVATE dwmapi)
    set_target_properties(SJJ-COM-Tool PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
