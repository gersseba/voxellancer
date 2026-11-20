cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

# PROJECT DESCRIPTION
set(META_PROJECT_NAME "voxellancer")
set(META_VERSION_MAJOR "0")
set(META_VERSION_MINOR "3")
set(META_VERSION_PATCH "1")
set(META_VERSION "${META_VERSION_MAJOR}.${META_VERSION_MINOR}.${META_VERSION_PATCH}")
set(META_AUTHOR_ORGANIZATION "voxelinc")
set(META_AUTHOR_DOMAIN "https://github.com/voxelinc/voxellancer/")

set(VOXELLANCER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# OPTIONS
option(OPTION_LIMIT_CONFIGS "Generate limited configs (Release; Debug)" ON)
option(OPTION_ERRORS_AS_EXCEPTION "Throw exceptions" OFF)
option(OPTION_CREATE_SUB_TEST "Create sub tests" OFF)
option(OPTION_INCLUDE_VC_REDIST "Include VC redist" ON)
option(VOXELLANCER_BUILD_CLIENT "Build the graphical game and network client" ON)
option(VOXELLANCER_BUILD_SERVER "Build the headless authoritative server" ON)
option(VOXELLANCER_BUILD_TESTS "Build tests" ON)

if(NOT VOXELLANCER_BUILD_CLIENT AND NOT VOXELLANCER_BUILD_SERVER AND NOT VOXELLANCER_BUILD_TESTS)
    message(FATAL_ERROR "At least one Voxellancer target must be enabled.")
endif()


# CMAKE CONFIGURATION
set(CMAKE_VERBOSE_MAKEFILE 1)
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Include cmake modules from ./cmake
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Set configuration types
if(OPTION_LIMIT_CONFIGS)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Limited Configs" FORCE)
endif()

# Project
project(${META_PROJECT_NAME} C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Include custom cmake functions
include(cmake/Custom.cmake)


# PLATFORM AND ARCHITECTURE

# Architecture (32/64 bit)
set(X64 OFF)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(X64 ON)
endif()

# Check for linux
if(UNIX AND NOT APPLE)
    set(LINUX 1)
endif()

# Setup platform specifics (compile flags, etc., ...)
if(MSVC)
    message(STATUS "Configuring for platform Windows/MSVC.")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformWindowsMSVC.cmake)
    add_definitions(
        -DWIN32
        -DUNICODE
        -D_UNICODE
        -D_SCL_SECURE_NO_WARNINGS
        -D_CRT_SECURE_NO_DEPRECATE
        -D_CRT_SECURE_NO_WARNINGS
    )
elseif(WIN32 AND CMAKE_COMPILER_IS_GNUCXX)
    message(STATUS "Configuring for platform Windows/GCC.")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformWindowsGCC.cmake)
elseif(LINUX AND CMAKE_COMPILER_IS_GNUCXX)
    message(STATUS "Configuring for platform Linux/GCC.")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformLinuxGCC.cmake)
elseif(APPLE)
    message(STATUS "Configuring for platform MacOS.")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformMacOS.cmake)
else()
    # Unsupported system/compiler
    message(WARNING "Unsupported platform/compiler combination")
endif()

# GET REPOSITORY VERSION
# Add a revision file containing the git-head tag for cpack and install
# include(GetGitRevisionDescription)
# get_git_head_revision(GIT_REFSPEC GIT_SHA1)

# Generate a shorter, googlelike variation for rev
# string(SUBSTRING ${GIT_SHA1} 0 12 GIT_REV)
# file(WRITE ${CMAKE_BINARY_DIR}/revision ${GIT_REV})


# DEPENDENCIES
# Find externals
find_package(glm CONFIG REQUIRED)
if(VOXELLANCER_BUILD_CLIENT)
    find_package(OpenGL REQUIRED)
    find_package(GLEW CONFIG REQUIRED)
    find_package(glfw3 CONFIG REQUIRED)
endif()

set(SFML_COMPONENTS)
if(VOXELLANCER_BUILD_CLIENT)
    list(APPEND SFML_COMPONENTS Audio)
endif()
if(VOXELLANCER_BUILD_CLIENT OR VOXELLANCER_BUILD_SERVER)
    list(APPEND SFML_COMPONENTS Network)
endif()
if(SFML_COMPONENTS)
    find_package(SFML 3 CONFIG REQUIRED COMPONENTS ${SFML_COMPONENTS})
endif()

if(VOXELLANCER_BUILD_CLIENT OR VOXELLANCER_BUILD_SERVER)
    find_package(Lua 5.5 REQUIRED)
    find_package(OpenMP REQUIRED)
endif()

if(NOT TARGET Lua::Lua)
    add_library(Lua::Lua INTERFACE IMPORTED)
    set_target_properties(Lua::Lua PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}"
    )
endif()

if(VOXELLANCER_BUILD_TESTS)
    if(NOT EXISTS "${LIB_DIR}/bandit-1.1.4/bandit/bandit.h" AND EXISTS "${LIB_DIR}/lib.zip")
        file(ARCHIVE_EXTRACT
            INPUT "${LIB_DIR}/lib.zip"
            DESTINATION "${LIB_DIR}"
            PATTERNS "bandit-1.1.4/*"
        )
    endif()
    find_package(BANDIT REQUIRED)
endif()

set(GLOW_SOURCE_DIR ${LIB_DIR}/glow)
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat)
if(VOXELLANCER_BUILD_CLIENT)
    include_directories(${GLOW_SOURCE_DIR}/source/glow/include)
    include_directories(${GLOW_SOURCE_DIR}/source/glowutils/include)
endif()

# Compile voxellancer
set(target voxellancer)

message(STATUS "${target}")
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(HEADER_GROUP "Header Files")
set(SOURCE_GROUP "Source Files")
set(SHADER_GROUP "Shader Files")

file(GLOB_RECURSE SRC_FILES
    "src/*.h"
    "src/*.inl"
    "src/*.cpp"
)

set(SRC_FILES_EXCEPT_MAIN ${SRC_FILES})
list(REMOVE_ITEM SRC_FILES_EXCEPT_MAIN ${SRC_DIR}/main.cpp)
list(REMOVE_ITEM SRC_FILES_EXCEPT_MAIN ${SRC_DIR}/server/main.cpp)
list(REMOVE_ITEM SRC_FILES_EXCEPT_MAIN
    ${SRC_DIR}/server/tcpserver.cpp
    ${SRC_DIR}/server/headlesslogmessagebuilder.cpp
    ${SRC_DIR}/client/main.cpp
    ${SRC_DIR}/client/tcpclient.cpp
    ${SRC_DIR}/client/snapshotinterpolator.cpp
    ${SRC_DIR}/protocol/serverprotocol.cpp
)

file(GLOB_RECURSE SHADER_FILES
    "data/*.vert"
    "data/*.frag"
    "data/*.geo"
)

source_group(${SHADER_GROUP} FILES ${SHADER_FILES})


source_group_by_path(${SOURCE_DIR} "\\.h$|\\.hpp$"
    ${HEADER_GROUP} ${SRC_FILES})
source_group_by_path(${SOURCE_DIR} "\\.cpp$|\\.c$|\\.inl$"
    ${SOURCE_GROUP} ${SRC_FILES})

# If we build for windows systems, we also include the resource file
# containing the manifest, icon and other resources
if(WIN32)
  set(SRC_FILES ${SRC_FILES} "${SOURCE_DIR}/voxellancer.rc")
endif(WIN32)

set(CLIENT_SOURCE_FILES
    ${SRC_FILES_EXCEPT_MAIN}
    ${SRC_DIR}/client/tcpclient.cpp
    ${SRC_DIR}/client/snapshotinterpolator.cpp
    ${SOURCE_DIR}/main.cpp
    ${SHADER_FILES}
)
set(SERVER_SOURCE_FILES
    ${SRC_FILES_EXCEPT_MAIN}
    ${SRC_DIR}/server/tcpserver.cpp
    ${SOURCE_DIR}/server/main.cpp
)
list(FILTER SERVER_SOURCE_FILES EXCLUDE REGEX
    "/(camera|display|gamestate|input|programs|sound|ui)/|/benchmark/benchmarkrunner\\.cpp$|/benchmark/benchmarkprofiler\\.cpp$|/etc/context(dependant|provider)\\.cpp$|/geometry/viewport\\.cpp$|/player\\.cpp$|/resource/ddstexture\\.cpp$|/scenarios/[^/]+\\.cpp$|/utils/(aabbrenderer|cubemesh)\\.cpp$|/utils/statemachine/|/voxel/(voxelrenderdata|voxelrenderer)\\.cpp$|/voxeleffect/particlechecks/|/voxeleffect/(voxelmesh|voxelparticleengine|voxelparticlerenderer)\\.cpp$|/sound/(sound|soundmanager)\\.cpp$")
list(REMOVE_ITEM SERVER_SOURCE_FILES
    "${SRC_DIR}/client/localserverprocess.cpp"
)
# Property loading still materializes InputMapping values without device input.
list(APPEND SERVER_SOURCE_FILES
    "${SRC_DIR}/benchmark/benchmarkprofiler.cpp"
    "${SRC_DIR}/display/rendering/visuals.cpp"
    "${SRC_DIR}/input/inputmapping.cpp"
    "${SRC_DIR}/scenarios/basescenario.cpp"
    "${SRC_DIR}/scenarios/battlescenario.cpp"
    "${SRC_DIR}/scenarios/largecombatscenario.cpp"
    "${SRC_DIR}/scenarios/aicomparisonscenario.cpp"
    "${SRC_DIR}/sound/soundproperties.cpp"
)
if(WIN32)
    list(APPEND CLIENT_SOURCE_FILES "${SOURCE_DIR}/voxellancer.rc")
endif()

add_library(voxellancer-protocol STATIC
    ${SRC_DIR}/protocol/serverprotocol.cpp
)
target_include_directories(voxellancer-protocol
    PUBLIC ${SOURCE_DIR}
)
target_link_libraries(voxellancer-protocol
    PUBLIC glm::glm
)

if(VOXELLANCER_BUILD_SERVER)
    set(GLOW_LOGGING_SOURCE_DIR ${GLOW_SOURCE_DIR}/source/glow/source)
    add_library(voxellancer-server-support STATIC
        ${GLOW_LOGGING_SOURCE_DIR}/Changeable.cpp
        ${GLOW_LOGGING_SOURCE_DIR}/ChangeListener.cpp
        ${GLOW_LOGGING_SOURCE_DIR}/ConsoleLogger.cpp
        ${GLOW_LOGGING_SOURCE_DIR}/LogMessage.cpp
        ${GLOW_LOGGING_SOURCE_DIR}/formatString.cpp
        ${GLOW_LOGGING_SOURCE_DIR}/logging.cpp
        ${SRC_DIR}/server/headlesslogmessagebuilder.cpp
    )
    target_include_directories(voxellancer-server-support
        PUBLIC ${GLOW_SOURCE_DIR}/source/glow/include
    )
    target_compile_definitions(voxellancer-server-support
        PUBLIC GLOW_STATIC
    )
    target_link_libraries(voxellancer-server-support
        PUBLIC glm::glm
    )
endif()

add_definitions(-DGLM_FORCE_RADIANS)
add_definitions(-DGLM_FORCE_CTOR_INIT)
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

if(VOXELLANCER_BUILD_CLIENT)
    add_executable(${target}
        ${CLIENT_SOURCE_FILES}
    )

    set(LIBS_TO_LINK
        OpenGL::GL
        GLEW::GLEW
        glfw
        glm::glm
        SFML::Audio
        Lua::Lua
        OpenMP::OpenMP_CXX
        glow
        glowutils
        ${LIBS_TO_LINK}
    )

    if(MSVC)
        list(APPEND LIBS_TO_LINK legacy_stdio_definitions)
    endif()

    target_link_libraries(${target}
        ${LIBS_TO_LINK}
        voxellancer-protocol
        SFML::Network
    )

    add_executable(voxellancer-client
        ${SRC_DIR}/client/main.cpp
        ${SRC_DIR}/client/tcpclient.cpp
        ${SRC_DIR}/client/snapshotinterpolator.cpp
    )
    target_link_libraries(voxellancer-client
        voxellancer-protocol
        glm::glm
        SFML::Network
    )
    set_target_properties(voxellancer-client
        PROPERTIES
        LINKER_LANGUAGE CXX
        COMPILE_DEFINITIONS_DEBUG "${DEFAULT_COMPILE_DEFS_DEBUG}"
        COMPILE_DEFINITIONS_RELEASE "${DEFAULT_COMPILE_DEFS_RELEASE}"
        COMPILE_FLAGS "${DEFAULT_COMPILE_FLAGS}"
        LINK_FLAGS_DEBUG "${DEFAULT_LINKER_FLAGS_DEBUG}"
        LINK_FLAGS_RELEASE "${DEFAULT_LINKER_FLAGS_RELEASE}"
        DEBUG_POSTFIX "d${DEBUG_POSTFIX}"
    )

    set_target_properties(${target}
        PROPERTIES
        LINKER_LANGUAGE CXX
        COMPILE_DEFINITIONS_DEBUG "${DEFAULT_COMPILE_DEFS_DEBUG}"
        COMPILE_DEFINITIONS_RELEASE "${DEFAULT_COMPILE_DEFS_RELEASE}"
        COMPILE_FLAGS "${DEFAULT_COMPILE_FLAGS}"
        LINK_FLAGS_DEBUG "${DEFAULT_LINKER_FLAGS_DEBUG}"
        LINK_FLAGS_RELEASE "${DEFAULT_LINKER_FLAGS_RELEASE}"
        DEBUG_POSTFIX "d${DEBUG_POSTFIX}"
    )
endif()
if(VOXELLANCER_BUILD_SERVER)
    set(SERVER_CORE_SOURCE_FILES ${SERVER_SOURCE_FILES})
    list(REMOVE_ITEM SERVER_CORE_SOURCE_FILES ${SOURCE_DIR}/server/main.cpp)
    list(APPEND SERVER_CORE_SOURCE_FILES
        ${SRC_DIR}/client/tcpclient.cpp
        ${SRC_DIR}/client/snapshotinterpolator.cpp
    )
    add_library(voxellancer-server-core STATIC
        ${SERVER_CORE_SOURCE_FILES}
    )
    target_compile_definitions(voxellancer-server-core
        PUBLIC VOXELLANCER_HEADLESS_SERVER=1
    )
    set(SERVER_LIBS_TO_LINK
        glm::glm
        Lua::Lua
        OpenMP::OpenMP_CXX
        voxellancer-server-support
    )
    if(MSVC)
        list(APPEND SERVER_LIBS_TO_LINK legacy_stdio_definitions)
    endif()
    target_link_libraries(voxellancer-server-core
        PUBLIC
        ${SERVER_LIBS_TO_LINK}
        voxellancer-protocol
        SFML::Network
    )
    set_target_properties(voxellancer-server-core
        PROPERTIES
        COMPILE_DEFINITIONS_DEBUG "${DEFAULT_COMPILE_DEFS_DEBUG}"
        COMPILE_DEFINITIONS_RELEASE "${DEFAULT_COMPILE_DEFS_RELEASE}"
        COMPILE_FLAGS "${DEFAULT_COMPILE_FLAGS}"
    )
    add_executable(voxellancer-server
        ${SOURCE_DIR}/server/main.cpp
    )
    target_link_libraries(voxellancer-server PRIVATE voxellancer-server-core)
    set_target_properties(voxellancer-server
        PROPERTIES
        LINKER_LANGUAGE CXX
        COMPILE_DEFINITIONS_DEBUG "${DEFAULT_COMPILE_DEFS_DEBUG}"
        COMPILE_DEFINITIONS_RELEASE "${DEFAULT_COMPILE_DEFS_RELEASE}"
        COMPILE_FLAGS "${DEFAULT_COMPILE_FLAGS}"
        LINK_FLAGS_DEBUG "${DEFAULT_LINKER_FLAGS_DEBUG}"
        LINK_FLAGS_RELEASE "${DEFAULT_LINKER_FLAGS_RELEASE}"
        DEBUG_POSTFIX "d${DEBUG_POSTFIX}"
    )
endif()

set(DOCUMENTATION doc/howto.html doc/luaapi.html)

set(INSTALL_TARGETS)
if(VOXELLANCER_BUILD_CLIENT)
    list(APPEND INSTALL_TARGETS ${target} voxellancer-client)
endif()
if(VOXELLANCER_BUILD_SERVER)
    list(APPEND INSTALL_TARGETS voxellancer-server)
endif()
if(INSTALL_TARGETS)
    install(TARGETS ${INSTALL_TARGETS} RUNTIME DESTINATION .)
endif()
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
        DESTINATION .
        PATTERN "*.png" EXCLUDE)
install(FILES ${BINARIES} DESTINATION .)
install(FILES ${DOCUMENTATION} DESTINATION .)

include(cmake/Package.cmake)

include_directories(${SOURCE_DIR})

if(VOXELLANCER_BUILD_TESTS)
    add_subdirectory(test)
endif()

# add GLOW and GLOWUTILS
set(INSTALL_BIN           ".")
set(INSTALL_SHARED        ".")
set(INSTALL_LIB           "lib")
if(APPLE)
    set(OPTION_BUILD_STATIC ON)
else()
    set(OPTION_BUILD_STATIC OFF)
endif()
option(OPTION_ERRORS_AS_EXCEPTION "Throw exceptions" OFF)

if(VOXELLANCER_BUILD_CLIENT)
    add_subdirectory(${GLOW_SOURCE_DIR}/source/glow)
    add_subdirectory(${GLOW_SOURCE_DIR}/source/glowutils)

    target_link_libraries(glow glm::glm)
    target_link_libraries(glowutils glm::glm)
    if(MSVC)
        target_compile_options(glowutils PRIVATE /wd4834)
    endif()
endif()
