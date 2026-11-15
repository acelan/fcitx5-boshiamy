add_library(boshiamy_dict STATIC
    dict.cpp
)
set_target_properties(boshiamy_dict PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(boshiamy_dict PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(boshiamy_dict PUBLIC Fcitx5::Utils)

set(BOSHIAMY_SOURCES
    engine.cpp
)
add_fcitx5_addon(boshiamy ${BOSHIAMY_SOURCES})
target_link_libraries(boshiamy boshiamy_dict Fcitx5::Core Fcitx5::Config)
target_compile_definitions(boshiamy PRIVATE FCITX_GETTEXT_DOMAIN=\"fcitx5-boshiamy\")
fcitx5_add_i18n_definition(TARGETS boshiamy)

install(TARGETS boshiamy DESTINATION "${FCITX_INSTALL_ADDONDIR}")

configure_file(boshiamy.conf.in "${CMAKE_CURRENT_BINARY_DIR}/boshiamy.conf" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/boshiamy.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")

configure_file(boshiamy-addon.conf.in.in "${CMAKE_CURRENT_BINARY_DIR}/boshiamy-addon.conf" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/boshiamy-addon.conf"
        RENAME boshiamy.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
