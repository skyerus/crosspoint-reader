add_executable(ClippingStoreTest ClippingStoreTest.cpp HighlightOutboxTest.cpp ${REPO_ROOT}/src/ClippingStore.cpp ${REPO_ROOT}/src/clippings/HighlightOutbox.cpp)
target_link_libraries(ClippingStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(ClippingStoreTest BEFORE PRIVATE stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Memory)
gtest_discover_tests(ClippingStoreTest)
