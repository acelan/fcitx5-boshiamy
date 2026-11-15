add_custom_target(copy-addon ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_BINARY_DIR}/src/boshiamy-addon.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/boshiamy.conf"
    DEPENDS "${PROJECT_BINARY_DIR}/src/boshiamy-addon.conf")
