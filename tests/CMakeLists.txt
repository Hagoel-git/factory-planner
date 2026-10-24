project(factory_planner_tests LANGUAGES CXX)

include(GoogleTest)

add_executable(
        factory_planner_tests
        test_factory_graph.cpp
        test_factory_solver.cpp
        test_undo_redo_system.cpp
        test_frame_pacer.cpp
        test_application.cpp
)

target_link_libraries(
        factory_planner_tests
        PRIVATE
        gtest_main
        factory_planner_lib
)

if(WIN32)
    add_custom_command(TARGET factory_planner_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/external/or-tools/bin"
            "$<TARGET_FILE_DIR:factory_planner_tests>"
            COMMENT "Copying DLLs to factory_planner_tests..."
    )
endif()

if (UNIX AND NOT APPLE)
    set_target_properties(factory_planner_tests PROPERTIES
            BUILD_WITH_INSTALL_RPATH OFF
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/external/or-tools/lib"
            INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

gtest_discover_tests(factory_planner_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/external/or-tools/lib"
)

