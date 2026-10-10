# Overlay port for the HotKV C++ SDK (hotkv-ltd/hotkv-cpp-client).
#
# This is an overlay port; it is not in the public vcpkg registry yet.
#
# SHA512 is a PLACEHOLDER. A real value only exists once a release tarball
# does (tag v${VERSION}): run `vcpkg install hotkv-cpp-client` once, copy the
# hash vcpkg reports into the line below, and commit it. To test before a
# release exists, point SOURCE_PATH at a local checkout.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO hotkv-ltd/hotkv-cpp-client
    REF "v${VERSION}"
    SHA512 0 # PLACEHOLDER: replace with the SHA512 of the v${VERSION} tarball
    HEAD_REF main
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        tls HOTKV_CPP_WITH_TLS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DHOTKV_CPP_BUILD_TESTS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME hotkv-cpp CONFIG_PATH lib/cmake/hotkv-cpp)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
