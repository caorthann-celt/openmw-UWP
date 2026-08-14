vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO caorthann-celt/collada-dom-UWP
    REF 1f19e026d15a7e7a19c58a393312bd9dc2194746
    SHA512 4e10504d8e3263ba20d26d0572e3f26e3f5f188133ac3e4ea9613ac66e273d3ed716507eb1c0093b8d38489ec7a01d84a8688798bc6f4d57cc17dce11350c11d
    HEAD_REF uwp
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DCMAKE_CXX_STANDARD=11
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/collada_dom-2.5)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/licenses/dom_license_e.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)

vcpkg_fixup_pkgconfig()
