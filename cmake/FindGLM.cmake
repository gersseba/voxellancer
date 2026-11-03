
find_package(glm CONFIG QUIET)

if(TARGET glm::glm)
    set(GLM_FOUND TRUE)
    get_target_property(GLM_INCLUDE_DIR glm::glm INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT GLM_INCLUDE_DIR OR GLM_INCLUDE_DIR MATCHES "-NOTFOUND$")
        set(GLM_INCLUDE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    endif()
else()
    set(GLM_FOUND FALSE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLM REQUIRED_VARS GLM_FOUND)
