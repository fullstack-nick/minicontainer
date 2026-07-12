cmake_minimum_required(VERSION 3.28)
project(minicontainer VERSION 0.0.1 LANGUAGES C)

set(MC_GIT_COMMIT "unknown" CACHE STRING "Git commit embedded in the binary")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_library(minicontainer_core STATIC
  src/image/import.c
  src/linux/cgroup.c
  src/linux/container.c
  src/linux/stats.c
  src/runtime/launch.c
  src/runtime/info.c
  src/util/error.c
  src/util/fs.c
  src/util/id.c
  src/util/resource.c
  src/util/subid.c
  src/util/validate.c)
target_include_directories(minicontainer_core PUBLIC include)
target_link_libraries(minicontainer_core PUBLIC PkgConfig::LIBARCHIVE PkgConfig::OPENSSL
  PkgConfig::SYSTEMD)
target_compile_definitions(minicontainer_core PRIVATE _GNU_SOURCE _XOPEN_SOURCE=700)

add_executable(minicontainer src/cli/main.c)
target_link_libraries(minicontainer PRIVATE minicontainer_core)
target_include_directories(minicontainer PRIVATE include)
add_executable(minicontainer-shim src/runtime/shim_main.c)
target_link_libraries(minicontainer-shim PRIVATE minicontainer_core)
target_include_directories(minicontainer-shim PRIVATE include)
target_compile_definitions(minicontainer PRIVATE
  MC_VERSION="${PROJECT_VERSION}"
  MC_GIT_COMMIT="${MC_GIT_COMMIT}")
target_compile_options(minicontainer_core PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wformat=2
  -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wwrite-strings)
target_compile_options(minicontainer PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wformat=2
  -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wwrite-strings)
target_compile_options(minicontainer-shim PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wformat=2
  -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wwrite-strings)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME version COMMAND minicontainer version)
  set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "minicontainer 0.0.1")
  add_test(NAME info COMMAND minicontainer info --json)
  set_tests_properties(info PROPERTIES PASS_REGULAR_EXPRESSION "cgroup_version")
  add_executable(unit_tests tests/unit/test_core.c)
  target_link_libraries(unit_tests PRIVATE minicontainer_core)
  target_compile_options(unit_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wformat=2
    -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wwrite-strings)
  add_test(NAME unit COMMAND unit_tests)
  add_test(NAME image_import
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration/test_image_import.sh
            $<TARGET_FILE:minicontainer>)
endif()

install(TARGETS minicontainer RUNTIME DESTINATION bin)
install(TARGETS minicontainer-shim RUNTIME DESTINATION libexec/minicontainer)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME minicontainer)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "MiniContainer maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGE_FILE_NAME "minicontainer_${PROJECT_VERSION}_amd64")
include(CPack)
