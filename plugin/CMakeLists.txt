cmake_minimum_required(VERSION 3.24)
project(obs-multicam-review VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- OBS SDK ----
# 用户通过 -DOBS_SDK_DIR=path 或环境变量 OBS_SDK_DIR 指定
if(NOT DEFINED OBS_SDK_DIR)
    # 默认路径 (OBS Studio 标准安装)
    if(WIN32)
        set(OBS_SDK_DIR "C:/Program Files/obs-studio")
    elseif(APPLE)
        set(OBS_SDK_DIR "/Library/Application Support/obs-studio")
    else()
        set(OBS_SDK_DIR "/usr")
    endif()
endif()

# ---- 查找 libobs ----
list(APPEND CMAKE_PREFIX_PATH "${OBS_SDK_DIR}")
find_path(LIBOBS_INCLUDE_DIR obs/obs.h
    HINTS "${OBS_SDK_DIR}/include" "${OBS_SDK_DIR}/include/obs")
find_library(LIBOBS_LIB obs
    HINTS "${OBS_SDK_DIR}/bin/64bit"
    "${OBS_SDK_DIR}/lib")

if(NOT LIBOBS_INCLUDE_DIR OR NOT LIBOBS_LIB)
    message(FATAL_ERROR "OBS SDK not found at ${OBS_SDK_DIR}. Set -DOBS_SDK_DIR=...")
endif()
message(STATUS "libobs include: ${LIBOBS_INCLUDE_DIR}")
message(STATUS "libobs library: ${LIBOBS_LIB}")

# ---- civetweb (内嵌 HTTP/WS 服务器) ----
# 从 third_party/civetweb 编译
set(CIVETWEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/civetweb")
if(EXISTS "${CIVETWEB_DIR}/civetweb.c")
    add_library(civetweb STATIC
        "${CIVETWEB_DIR}/civetweb.c"
    )
    target_include_directories(civetweb PUBLIC "${CIVETWEB_DIR}")
    target_compile_definitions(civetweb PRIVATE
        CIVETWEB_ENABLE_WEBSOCKETS=1
        NO_SSL=1           # 先禁 SSL，后续可开
        USE_IPV6=0
    )
else()
    message(WARNING "civetweb not found, will build without web server")
endif()

# ---- SQLite3 ----
set(SQLITE3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3")
if(EXISTS "${SQLITE3_DIR}/sqlite3.c")
    add_library(sqlite3 STATIC "${SQLITE3_DIR}/sqlite3.c")
    target_include_directories(sqlite3 PUBLIC "${SQLITE3_DIR}")
    target_compile_definitions(sqlite3 PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_ENABLE_FTS5=1
    )
else()
    message(WARNING "SQLite3 not found, will build without database")
endif()

# ---- nlohmann/json (header-only) ----
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    set(NLOHMANN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include")
    if(EXISTS "${NLOHMANN_DIR}")
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE "${NLOHMANN_DIR}")
    endif()
endif()

# ---- 插件源文件 ----
file(GLOB_RECURSE PLUGIN_SOURCES
    "src/*.cpp"
    "src/*.h"
)

add_library(obs-multicam-review SHARED ${PLUGIN_SOURCES})

target_include_directories(obs-multicam-review PRIVATE
    ${LIBOBS_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(obs-multicam-review PRIVATE
    ${LIBOBS_LIB}
)

if(TARGET civetweb)
    target_link_libraries(obs-multicam-review PRIVATE civetweb)
    target_compile_definitions(obs-multicam-review PRIVATE HAS_CIVETWEB=1)
endif()

if(TARGET sqlite3)
    target_link_libraries(obs-multicam-review PRIVATE sqlite3)
    target_compile_definitions(obs-multicam-review PRIVATE HAS_SQLITE=1)
endif()

if(TARGET nlohmann_json)
    target_link_libraries(obs-multicam-review PRIVATE nlohmann_json)
endif()

# ---- Windows 特定 ----
if(WIN32)
    target_link_libraries(obs-multicam-review PRIVATE
        ws2_32      # civetweb 需要 Winsock
        winmm       # timeGetTime
        user32
        gdi32
    )
    set_target_properties(obs-multicam-review PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
endif()

# ---- 安装到 OBS 插件目录 ----
install(TARGETS obs-multicam-review
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
)

# ---- 复制 Web UI 到 data 目录 ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../control-ui/dist")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../control-ui/dist/"
        DESTINATION "data/obs-plugins/obs-multicam-review/web"
    )
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../overlay/"
    DESTINATION "data/obs-plugins/obs-multicam-review/overlay"
)

message(STATUS "obs-multicam-review configured successfully")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
