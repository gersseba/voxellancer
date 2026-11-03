
set(LINUX_COMPILE_DEFS
	LINUX	                  # Linux system
	PIC		                  # Position-independent code
	_REENTRANT                # Reentrant code
)
set(DEFAULT_COMPILE_DEFS_DEBUG
    ${LINUX_COMPILE_DEFS}
    _DEBUG	                  # Debug build
)
set(DEFAULT_COMPILE_DEFS_RELEASE
    ${LINUX_COMPILE_DEFS}
    NDEBUG	                  # Release build
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")

set(LINUX_COMPILE_FLAGS "-pthread -pipe -fPIC -Wreturn-type -Wall -w -pedantic -Wextra -Wtrampolines -Wfloat-equal -Wshadow -Wcast-qual -Wcast-align -Wconversion -Werror ${EXCEPTION_FLAG}")
# pthread       -> use pthread library
# no-rtti       -> disable c++ rtti
# pipe          -> use pipes
# fPIC          -> use position independent code
# -Wreturn-type -Werror=return-type -> missing returns in functions and methods are handled as errors which stops the compilation

set(DEFAULT_COMPILE_FLAGS ${LINUX_COMPILE_FLAGS})

set(LIBS_TO_LINK ${LIBS_TO_LINK} pthread Xxf86vm X11 Xrandr Xi )	

# Add platform specific libraries for linking
set(EXTRA_LIBS "")
