
find_package(GLEW CONFIG QUIET)

if(TARGET GLEW::GLEW)
    set(GLEW_FOUND TRUE)
    set(GLEW_LIBRARY GLEW::GLEW)
    get_target_property(GLEW_INCLUDE_DIR GLEW::GLEW INTERFACE_INCLUDE_DIRECTORIES)
else()
    set(GLEW_FOUND FALSE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLEW REQUIRED_VARS GLEW_FOUND)
