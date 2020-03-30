cmake_minimum_required(VERSION 3.8)

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/cmake/cable/.git)
    message(FATAL_ERROR "Git submodules not initialized, execute:\n  git submodule update --init")
endif()

include(cmake/cable/bootstrap.cmake)
include(CableBuildInfo)
include(CableBuildType)
include(CableCompilerSettings)
include(CableToolchains)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

cable_configure_toolchain(DEFAULT cxx17-pic)
#set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)
#set(CMAKE_POSITION_INDEPENDENT_CODE ON)

cable_set_build_type(DEFAULT RelWithDebInfo CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)

set(CMAKE_DEBUG_POSTFIX "")

project(athena)
set(PROJECT_VERSION 0.1.0-alpha)

cable_configure_compiler()
if(CABLE_COMPILER_GNULIKE)
    # TODO: fix the warnings instead
    add_compile_options(-Wno-pedantic)
    add_compile_options(-Wno-sign-conversion)
    add_compile_options(-Wno-sign-compare)
    add_compile_options(-Wno-conversion)
    add_compile_options(-Wno-unused-parameter)
    add_compile_options(-Wno-error)
endif()


cable_add_buildinfo_library(PROJECT_NAME athena EXPORT athenaTargets)

option(BUILD_SHARED_LIBS "Build libraries as shared" ON)

if(CABLE_COMPILER_CLANG)
    option(ATHENA_FUZZING "Build Athena fuzzer" OFF)
endif()
if(ATHENA_FUZZING)
    set(fuzzer_flags -fsanitize=fuzzer,undefined,address)
    add_compile_options(${fuzzer_flags})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${fuzzer_flags}")
endif()

option(H_WABT "Build with wabt" ON)
if (H_WABT)
    include(ProjectWabt)
endif()

option(H_EOS "Build with eos-vm" OFF)
if (H_EOS)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
#    add_compile_options(-pthread)
    include_directories ( eos-vm/include )
endif()

if (NOT (H_WABT OR H_EOS))
    message(FATAL_ERROR "At least one one engine must be enabled.")
endif()

find_package(evmc)
if (evmc_FOUND)
	message(STATUS "evmc FOUND")
else()
	message(FATAL_ERROR "evmc not FOUND")
endif()

#add_subdirectory(evmc)
add_subdirectory(src)
add_subdirectory(test)


install(DIRECTORY include/athena DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(athenaConfigVersion.cmake COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    cmake/Config.cmake.in
    athenaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/athena
)

install(
    EXPORT athenaTargets
    NAMESPACE athena::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/athena
)

install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/athenaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/athenaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/athena
)

if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_FILE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
include(CPack)
