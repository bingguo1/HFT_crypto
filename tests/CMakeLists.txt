add_executable(test_order_book       test_order_book.cpp)
add_executable(test_avellaneda_stoikov test_avellaneda_stoikov.cpp)
add_executable(test_ring_buffer      test_ring_buffer.cpp)

foreach(t test_order_book test_avellaneda_stoikov test_ring_buffer)
    target_link_libraries(${t} PRIVATE hfmm_core GTest::gtest GTest::gtest_main)
    target_include_directories(${t} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
