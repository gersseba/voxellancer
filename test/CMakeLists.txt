
include_directories(${BANDIT_INCLUDE_DIR})

if(VOXELLANCER_BUILD_CLIENT)
    set(TEST_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
                  ${CMAKE_CURRENT_SOURCE_DIR}/bandit_extension/vec3helper.cpp)
    include(maketest.cmake)

    if(${OPTION_CREATE_SUB_TEST})
        add_subdirectory(ai)
        add_subdirectory(geometry)
        add_subdirectory(worldtree)
        add_subdirectory(damageforwarder)
        add_subdirectory(property)
        add_subdirectory(voxelcluster)
        add_subdirectory(voxeltree)
        add_subdirectory(collision)
        add_subdirectory(directoryreader)
        add_subdirectory(loader)
        add_subdirectory(worldobject)
        add_subdirectory(worldtransform)
        add_subdirectory(splitting)
        add_subdirectory(scripting)
        add_subdirectory(statemachine)
        add_subdirectory(threading)
    endif()

    file(GLOB_RECURSE ALL_TEST_FILES
        "*/*.cpp"
    )
    list(APPEND ALL_TEST_FILES
        ${SRC_DIR}/server/tcpserver.cpp
        ${SRC_DIR}/client/tcpclient.cpp
        ${SRC_DIR}/client/inputpredictor.cpp
        ${SRC_DIR}/client/snapshotinterpolator.cpp
    )

    make_test(all_tests ${ALL_TEST_FILES})

    add_executable(menu_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/testmainmenustate.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/testcontactmarker.cpp
    )
    target_include_directories(menu_tests PRIVATE ${SRC_DIR})
    target_link_libraries(menu_tests PRIVATE glm::glm)
    set_target_properties(menu_tests PROPERTIES FOLDER "UnitTest")
endif()

add_executable(protocol_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/testserverprotocol.cpp
)
target_link_libraries(protocol_tests
    PRIVATE voxellancer-protocol
)
set_target_properties(protocol_tests
    PROPERTIES
    FOLDER "UnitTest"
)

if(VOXELLANCER_BUILD_SERVER)
    add_executable(server_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/server/testtcpserver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ai/testfighttask.cpp
    )
    target_link_libraries(server_tests
        PRIVATE voxellancer-server-core
    )
    set_target_properties(server_tests
        PROPERTIES
        FOLDER "UnitTest"
    )
endif()
