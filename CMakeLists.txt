cmake_minimum_required(VERSION 3.28)
project(minicontainer VERSION 0.0.1 LANGUAGES C)

set(MC_GIT_COMMIT "unknown" CACHE STRING "Git commit embedded in the binary")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_executable(minicontainer src/cli/main.c src/runtime/info.c)
target_include_directories(minicontainer PRIVATE include)
target_compile_definitions(minicontainer PRIVATE
  MC_VERSION="${PROJECT_VERSION}"
  MC_GIT_COMMIT="${MC_GIT_COMMIT}")
target_compile_options(minicontainer PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wformat=2
  -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wwrite-strings)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME version COMMAND minicontainer version)
  set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "minicontainer 0.0.1")
  add_test(NAME info COMMAND minicontainer info --json)
  set_tests_properties(info PROPERTIES PASS_REGULAR_EXPRESSION "cgroup_version")
endif()

install(TARGETS minicontainer RUNTIME DESTINATION bin)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME minicontainer)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "MiniContainer maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGE_FILE_NAME "minicontainer_${PROJECT_VERSION}_amd64")
include(CPack)
