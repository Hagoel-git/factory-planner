project(factory_planner_tests LANGUAGES CXX)

include(GoogleTest)

add_executable(
        run_tests
        test_factory_graph.cpp
        test_factory_solver.cpp
        test_undo_redo_system.cpp
        test_frame_pacer.cpp
        test_application.cpp
)

target_link_libraries(
        run_tests
        PRIVATE
        gtest_main
        factory_planner_lib
)

if(WIN32)
    add_custom_command(TARGET run_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/external/or-tools/bin"
            "$<TARGET_FILE_DIR:run_tests>"
            COMMENT "Copying DLLs to run_tests..."
    )
endif()

gtest_discover_tests(run_tests DISCOVERY_MODE PRE_TEST)
