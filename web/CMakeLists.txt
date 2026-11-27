cmake_minimum_required(VERSION 3.21)
project(voxellancer-web C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure the web build with emcmake.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(glm CONFIG REQUIRED)
find_package(Lua 5.5 REQUIRED)

include(FetchContent)
FetchContent_Declare(glew_source
    URL https://github.com/nigels-com/glew/releases/download/glew-2.3.1/glew-2.3.1.tgz
    URL_HASH SHA512=cb4caecf32ec0f180c2691dc7769ffc99571c64f259a2663a2b80e788f1c2fd5362c59e0caaeefed6fb78a4070366d244666a657358049b09071b59fae2377e0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(glew_source)
if(NOT glew_source_POPULATED)
    FetchContent_Populate(glew_source)
endif()
add_library(web-glew STATIC "${glew_source_SOURCE_DIR}/src/glew.c")
target_include_directories(web-glew PUBLIC "${glew_source_SOURCE_DIR}/include")
target_compile_definitions(web-glew PUBLIC GLEW_STATIC GLEW_NO_GLU PRIVATE GLEW_EGL)

file(GLOB GLOW_SOURCES "${ROOT}/lib/glow/source/glow/source/*.cpp")
list(FILTER GLOW_SOURCES EXCLUDE REGEX "/contextid\\.cpp$")
add_library(glow STATIC ${GLOW_SOURCES} "${ROOT}/src/web/contextid.cpp")
target_include_directories(glow PUBLIC
    "${ROOT}/cmake/compat"
    "${ROOT}/lib/glow/source/glow/include"
    "${ROOT}/lib/glow/source/codegeneration")
target_compile_definitions(glow PUBLIC GLOW_STATIC GLM_FORCE_RADIANS GLM_FORCE_CTOR_INIT GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(glow PUBLIC web-glew glm::glm)

file(GLOB GLOWUTILS_SOURCES
    "${ROOT}/lib/glow/source/glowutils/source/*.cpp"
    "${ROOT}/lib/glow/source/glowutils/source/math/*.cpp")
add_library(glowutils STATIC ${GLOWUTILS_SOURCES})
target_include_directories(glowutils PUBLIC "${ROOT}/lib/glow/source/glowutils/include")
target_compile_definitions(glowutils PUBLIC GLOWUTILS_STATIC)
target_link_libraries(glowutils PUBLIC glow)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${ROOT}/src/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX
    "/src/main\\.cpp$|/client/main\\.cpp$|/server/main\\.cpp$|/server/headlesslogmessagebuilder\\.cpp$|/client/localserverprocess\\.cpp$|/sound/sound\\.cpp$|/web/contextid\\.cpp$")
add_executable(voxellancer-web ${GAME_SOURCES})
target_include_directories(voxellancer-web PRIVATE "${ROOT}/src" "${ROOT}" "${LUA_INCLUDE_DIR}")
target_compile_options(voxellancer-web PRIVATE -fexceptions -sUSE_GLFW=3)
target_link_libraries(voxellancer-web PRIVATE glowutils ${LUA_LIBRARIES})
target_link_options(voxellancer-web PRIVATE
    -fexceptions -sUSE_GLFW=3 -sGL_ENABLE_GET_PROC_ADDRESS=1
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -lidbfs.js
    "--shell-file=${ROOT}/web/shell.html"
    "--pre-js=${ROOT}/web/persistence.js"
    "--preload-file=${ROOT}/data@/data")
set_target_properties(voxellancer-web PROPERTIES SUFFIX ".html")
