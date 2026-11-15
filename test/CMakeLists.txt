configure_file(testdir.h.in "${CMAKE_CURRENT_BINARY_DIR}/testdir.h" @ONLY)

add_subdirectory(addon)
add_subdirectory(inputmethod)

add_custom_target(copy-data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/boshiamy"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/boshiamy.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/boshiamy/boshiamy.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/chewing_word.csv"
        "${CMAKE_CURRENT_BINARY_DIR}/boshiamy/chewing_word.csv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/t2s.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/boshiamy/t2s.txt"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/data/boshiamy.txt"
        "${PROJECT_SOURCE_DIR}/data/chewing_word.csv"
        "${PROJECT_SOURCE_DIR}/data/t2s.txt")

add_executable(testboshiamy testboshiamy.cpp)
target_include_directories(testboshiamy PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(testboshiamy Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testboshiamy boshiamy copy-addon copy-im copy-data)
add_test(NAME testboshiamy COMMAND testboshiamy)
