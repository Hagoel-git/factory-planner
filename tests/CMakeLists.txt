project(factory_planner_tests LANGUAGES CXX)

include(GoogleTest)

add_executable(
        run_tests
        test_factory_graph.cpp
        test_factory_solver.cpp
)

target_link_libraries(
        run_tests
        PRIVATE
        gtest_main
        factory_planner_lib
)

gtest_discover_tests(run_tests)