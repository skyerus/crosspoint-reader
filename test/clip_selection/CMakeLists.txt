add_executable(ClipSelectionTest ClipSelectionTest.cpp ${REPO_ROOT}/src/activities/reader/ClipSelectionActivity.cpp)
target_link_libraries(ClipSelectionTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(ClipSelectionTest BEFORE PRIVATE stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
gtest_discover_tests(ClipSelectionTest)
