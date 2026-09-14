cmake_minimum_required(VERSION 3.14)

# Get version from git tag if available, fallback to VERSION file
find_package(Git)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(GIT_TAG)
    string(REGEX REPLACE "^v" "" PROJECT_VERSION ${GIT_TAG})
  else()
    # Fallback to VERSION file
    if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
      file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
      string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
    else()
      set(PROJECT_VERSION "0.1.0")
    endif()
  endif()
else()
  # Fallback to VERSION file
  if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
    string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
  else()
    set(PROJECT_VERSION "0.1.0")
  endif()
endif()

project(AgentwareAgent VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# Configure version header
configure_file(src/include/version.h.in include/version.h @ONLY)

# Include FetchContent for downloading dependencies
include(FetchContent)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
    add_compile_options(/W4 /WX /MP /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:DebugDLL>")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage")
    endif()
endif()

# Clang-Tidy configuration (disabled due to libpqxx compatibility issues)
# find_program(CLANG_TIDY_EXE NAMES clang-tidy)
# if(CLANG_TIDY_EXE)
#     set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
#     message(STATUS "Clang-Tidy found: ${CLANG_TIDY_EXE}")
# else()
#     message(STATUS "Clang-Tidy not found. Please install it via your system's package manager (e.g., 'brew install llvm' or 'apt install clang-tidy').")
# endif()

# Find dependencies
find_package(Threads REQUIRED)

# Find zlib with platform-specific logic, fallback to FetchContent
set(zlib_is_found FALSE)
if(APPLE)
    # On macOS, prefer pkg-config as it's more reliable with Homebrew
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZLIB QUIET IMPORTED_TARGET zlib)
        if(ZLIB_FOUND)
            message(STATUS "Found zlib using pkg-config: ${ZLIB_LINK_LIBRARIES}")
            set(ZLIB_TARGET PkgConfig::ZLIB)
            set(zlib_is_found TRUE)
        endif()
    endif()
elseif(WIN32)
    # On Windows, first try to find zlib via vcpkg
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        message(STATUS "Found zlib via vcpkg: ${ZLIB_LIBRARIES}")
        # Create an imported target for zlib if it doesn't exist
        if(NOT TARGET zlib_imported)
            add_library(zlib_imported INTERFACE IMPORTED)
            target_link_libraries(zlib_imported INTERFACE ${ZLIB_LIBRARIES})
            target_include_directories(zlib_imported INTERFACE ${ZLIB_INCLUDE_DIRS})
        endif()
        set(ZLIB_TARGET zlib_imported)
        set(zlib_is_found TRUE)
    endif()
else()
    # Linux/other Unix-like systems
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        set(ZLIB_TARGET ZLIB::ZLIB)
        set(zlib_is_found TRUE)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(ZLIB QUIET IMPORTED_TARGET zlib)
            if(ZLIB_FOUND)
                set(ZLIB_TARGET PkgConfig::ZLIB)
                set(zlib_is_found TRUE)
            endif()
        endif()
    endif()
endif()

if(NOT zlib_is_found)
    message(STATUS "zlib not found, downloading using FetchContent...")
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3
    )
    FetchContent_MakeAvailable(zlib)
    set(ZLIB_TARGET zlib)
    set(zlib_is_found TRUE)
endif()

# Find libcurl
find_package(CURL REQUIRED)

# Find OpenSSL, fallback to FetchContent
set(openssl_is_found FALSE)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
    # Set deployment target based on environment
    if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
        # Use compatible version for CI environments
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
    else()
        # Use current system version to avoid linker warnings with Homebrew libraries
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()

    # Use Homebrew's OpenSSL on macOS/arm64
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        # Try to find Homebrew's OpenSSL
        find_program(HOMEBREW NAMES brew)
        if(HOMEBREW)
            execute_process(
                COMMAND ${HOMEBREW} --prefix openssl@3
                OUTPUT_VARIABLE OPENSSL_ROOT_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(EXISTS ${OPENSSL_ROOT_DIR})
                set(OPENSSL_ROOT_DIR ${OPENSSL_ROOT_DIR} CACHE PATH "OpenSSL root directory")
                find_package(OpenSSL QUIET)
                if(OpenSSL_FOUND)
                    set(openssl_is_found TRUE)
                    message(STATUS "Using Homebrew's OpenSSL: ${OPENSSL_INCLUDE_DIR}")
                endif()
            endif()

            # Also find Homebrew's curl if available
            execute_process(
                COMMAND ${HOMEBREW} --prefix curl
                OUTPUT_VARIABLE CURL_ROOT_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(EXISTS ${CURL_ROOT_DIR})
                set(CURL_INCLUDE_DIRS ${CURL_ROOT_DIR}/include)
                set(CURL_LIBRARIES ${CURL_ROOT_DIR}/lib/libcurl.dylib)
                message(STATUS "Using Homebrew's curl: ${CURL_ROOT_DIR}")
            endif()
        endif()
    endif()

    if(NOT openssl_is_found)
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(openssl_is_found TRUE)
        endif()
    endif()
else()
    # On other platforms, use system OpenSSL
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(openssl_is_found TRUE)
    endif()


endif()

if(NOT openssl_is_found)
    message(STATUS "OpenSSL not found, downloading using FetchContent...")
    FetchContent_Declare(
        openssl
        GIT_REPOSITORY https://github.com/openssl/openssl.git
        GIT_TAG openssl-3.2.1
    )
    set(OPENSSL_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(openssl)
    add_library(OpenSSL::SSL ALIAS ssl)
    add_library(OpenSSL::Crypto ALIAS crypto)
    set(OPENSSL_INCLUDE_DIR ${openssl_SOURCE_DIR}/include)
    set(openssl_is_found TRUE)
endif()

# Add compiler and linker flags for macOS
if(APPLE)
    add_compile_options(-march=armv8.4-a+fp16+rcpc+dotprod+crypto)
    add_link_options(-Wl,-no_weak_imports)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_MACOSX_RPATH TRUE)
endif()

# Make sure we have the curl include directory
if(NOT CURL_INCLUDE_DIRS)
    set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})
endif()

message(STATUS "Using curl: ${CURL_INCLUDE_DIRS} : ${CURL_LIBRARIES}")

# Find nlohmann_json
find_package(nlohmann_json 3.11.2 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, downloading using FetchContent...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Find libpqxx (optional)
find_library(PQXX_LIBRARY pqxx)
find_path(PQXX_INCLUDE_DIR pqxx/pqxx)
if(PQXX_LIBRARY AND PQXX_INCLUDE_DIR)
    set(HAVE_PQXX TRUE)
    message(STATUS "Found libpqxx: ${PQXX_LIBRARY}")
else()
    set(HAVE_PQXX FALSE)
    message(WARNING "libpqxx not found. Database support will be disabled. Install libpqxx-dev to enable PostgreSQL support.")
endif()

# Find CPR - try to find the package first
set(CPR_MIN_VERSION "1.10.0")
set(CPR_FOUND FALSE)

# First try to find CPR using config mode
find_package(cpr ${CPR_MIN_VERSION} QUIET)

if(TARGET cpr::cpr)
    message(STATUS "Found CPR (config mode): ${cpr_VERSION}")
    set(CPR_FOUND TRUE)
else()
    # If not found, try to find the library directly
    find_library(CPR_LIBRARY NAMES cpr)
    find_path(CPR_INCLUDE_DIR NAMES cpr/cpr.h)

    if(CPR_LIBRARY AND CPR_INCLUDE_DIR)
        message(STATUS "Found CPR (library): ${CPR_LIBRARY}")
        add_library(cpr::cpr INTERFACE IMPORTED)
        target_include_directories(cpr::cpr INTERFACE ${CPR_INCLUDE_DIR})
        target_link_libraries(cpr::cpr INTERFACE
            ${CPR_LIBRARY}
            CURL::libcurl
            OpenSSL::SSL
            OpenSSL::Crypto
            ${ZLIB_TARGET}
        )
        set(CPR_FOUND TRUE)
    endif()
endif()

if(NOT CPR_FOUND)
    # If still not found, try to fetch it using FetchContent
    message(STATUS "CPR not found, downloading using FetchContent...")
    include(FetchContent)

    FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.10.5  # Updated to a more recent version
    )

    # Prevent building tests and examples
    set(CPR_BUILD_TESTS OFF CACHE BOOL "")
    set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "")
    set(CPR_BUILD_TESTS_VERBOSE OFF CACHE BOOL "")

    # Build CPR as static library for better linking compatibility
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

    # Add compiler flags to suppress deprecation warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
    endif()

    # Make sure we're using a compatible version of CPR
    set(CPR_CURL_NOSIGNAL ON CACHE BOOL "")

    # Make the directory to suppress warnings about missing directory
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps/cpr-build")

    FetchContent_MakeAvailable(cpr)

    # Add the include directory if it exists
    if(EXISTS "${CMAKE_BINARY_DIR}/_deps/cpr-src/include")
        target_include_directories(cpr PUBLIC "${CMAKE_BINARY_DIR}/_deps/cpr-src/include")
    endif()

    if(TARGET cpr::cpr)
        message(STATUS "CPR downloaded and built from source")
        set(CPR_FOUND TRUE)
    else()
        message(FATAL_ERROR "Failed to find or build CPR. Please install it manually or check your network connection.")
    endif()
endif()
# Enable testing
enable_testing()

# Fetch Google Test
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Only fetch and build gtest if not already found
if(NOT TARGET gtest)
    message(STATUS "Downloading and building Google Test...")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )

     # For Windows: Prevent overriding the parent project's compiler/linker settings
     set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

     # Build static libraries to avoid runtime dependencies
     set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

     # Make sure we're not overriding compiler flags
     set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
     set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
     set(BUILD_GTEST ON CACHE BOOL "" FORCE)

     FetchContent_MakeAvailable(googletest)

    # Create an interface library for gtest to make it easier to link
    if(NOT TARGET gtest_all_tests)
        add_library(gtest_all_tests INTERFACE)
        target_link_libraries(gtest_all_tests
            INTERFACE
                GTest::GTest
                GTest::Main
        )
    endif()
endif()

# Only set up the cpr target if we're building it ourselves
if(TARGET cpr)
    # Ensure proper linking on all platforms for our local cpr build
    target_link_libraries(cpr PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        ${ZLIB_TARGET}  # Use the appropriate zlib target for this platform
    )
endif()

# On macOS, add additional settings
if(APPLE)
    # Set minimum deployment target (already set above)
    # Use system version to avoid linker warnings with Homebrew libraries

    # Set rpath
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_MACOSX_RPATH TRUE)

    # Add compiler and linker flags to all targets
    add_compile_options(
        -march=armv8.4-a+fp16+rcpc+dotprod+crypto
        -Wno-deprecated-declarations
    )

    add_link_options(
        -Wl,-no_weak_imports
    )

    # Add these flags to the main target as well
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8.4-a+fp16+rcpc+dotprod+crypto -Wno-deprecated-declarations")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_weak_imports")

    # Add OpenSSL include directories to all targets
    include_directories(${OPENSSL_INCLUDE_DIR})

    # Add OpenSSL definitions to all targets
    add_compile_definitions(
        CURL_USE_OPENSSL=1
        CURL_USE_DARWINSSL=0
        CURL_USE_SECTRANSP=0
    )
endif()

# Include directories
include_directories(src/include)

# Implementation files live in one source directory.
file(GLOB ALL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Create library with all sources
add_library(agentware_lib ${ALL_SOURCES})
target_include_directories(agentware_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    $<$<BOOL:${HAVE_PQXX}>:${PQXX_INCLUDE_DIR}>
)

# Set compile definitions for symbol visibility
target_compile_definitions(agentware_lib PRIVATE AGENTWARE_LIBRARY $<$<BOOL:${HAVE_PQXX}>:HAVE_PQXX>)
set_target_properties(agentware_lib PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Set platform-specific compiler flags
if(MSVC)
    target_compile_options(agentware_lib PRIVATE /W4 /WX /EHsc)
    target_compile_definitions(agentware_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(agentware_lib PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        CXX_VISIBILITY_PRESET default
    )
    target_compile_definitions(agentware_lib PUBLIC "AGENTWARE_API=__declspec(dllexport)")
else()
    target_compile_options(agentware_lib PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(agentware_lib PUBLIC "AGENTWARE_API=__attribute__((visibility(\"default\")))")
endif()

# Link dependencies as PRIVATE to avoid exposing them
target_link_libraries(agentware_lib PRIVATE
    ${CURL_LIBRARIES}
    cpr::cpr
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    ${ZLIB_TARGET}
    ${CMAKE_DL_LIBS}
    Threads::Threads
    $<$<BOOL:${HAVE_PQXX}>:${PQXX_LIBRARY}>
)

# Set additional linker flags for macOS
if(APPLE)
    target_link_options(agentware_lib PRIVATE -stdlib=libc++)
    target_link_libraries(agentware_lib PRIVATE "-framework CoreFoundation" "-framework Security")
endif()

# Main executable
add_executable(agentware-agent src/main.cpp)
target_include_directories(agentware-agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(agentware-agent PRIVATE
    agentware_lib
)

# Test executable
add_executable(agentware-tests src/tests/main_test.cpp src/version.cpp)
target_include_directories(agentware-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(agentware-tests PRIVATE
    agentware_lib
    gtest_main
    gtest
)
add_test(NAME basic_tests COMMAND agentware-tests)

# Set target properties for test executable
set_target_properties(agentware-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    MACOSX_RPATH ON
    INSTALL_RPATH "@executable_path/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# Set target properties
set_target_properties(agentware-agent PROPERTIES
    OUTPUT_NAME "agentware-agent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_RPATH ON
    INSTALL_RPATH "@executable_path/../lib"
    BUILD_WITH_INSTALL_RPATH FALSE
    SKIP_BUILD_RPATH FALSE
)

# On macOS, add additional settings
if(APPLE)
    target_link_options(agentware-agent PRIVATE
        -Wl,-rpath,@loader_path/../lib
        -Wl,-rpath,/usr/local/lib
        -Wl,-rpath,/opt/homebrew/lib
    )
endif()

# Installation
install(TARGETS agentware-agent
    RUNTIME DESTINATION bin
)

# Install configuration files (only if .env.example exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/.env.example")
    install(FILES .env.example
        DESTINATION etc/agentware
        RENAME config.env
    )
endif()

# CPack configuration for package generation
set(CPACK_PACKAGE_NAME "agentware-agent")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Professional AI Agent with Command Execution")
set(CPACK_PACKAGE_DESCRIPTION "A powerful, modular AI agent built with modern C++ that provides seamless interaction with language models and comprehensive system operations.")
set(CPACK_PACKAGE_VENDOR "bniladridas")
set(CPACK_PACKAGE_CONTACT "bniladridas@gmail.com")

# Platform-specific packaging
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcpr1, nlohmann-json3-dev")
    set(CPACK_RPM_PACKAGE_REQUIRES "cpr-devel, json-devel")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
endif()

include(CPack)



# Add custom check target for testing and validation
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target agentware-agent
    COMMAND echo "Running automated tests..."
    COMMAND printf "2\\nversion\\nhelp\\nexit\\n" | ${CMAKE_BINARY_DIR}/bin/agentware-agent
    COMMAND echo "Checking binary exists..."
    COMMAND test -f ${CMAKE_BINARY_DIR}/bin/agentware-agent
    COMMAND echo "Validating executable permissions..."
    COMMAND test -x ${CMAKE_BINARY_DIR}/bin/agentware-agent
    COMMAND echo "All checks passed!"
    DEPENDS agentware-agent
    COMMENT "Running comprehensive project validation"
)

# Add quick test target
add_custom_target(test-quick
    COMMAND printf "2\\nexit\\n" | ${CMAKE_BINARY_DIR}/bin/agentware-agent
    DEPENDS agentware-agent
    COMMENT "Quick functionality test"
)

# Clang-Tidy target (disabled)
# if(CLANG_TIDY_EXE)
#     add_custom_target(clang-tidy
#         COMMAND ${CLANG_TIDY_EXE} --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy -p ${CMAKE_BINARY_DIR} ${ALL_SOURCES}
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         COMMENT "Running clang-tidy on all source files"
#     )
# endif()

# Code coverage target (requires lcov)
find_program(LCOV_EXE lcov)
find_program(GENHTML_EXE genhtml)
if(LCOV_EXE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
        COMMAND ctest --output-on-failure
        COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file ${CMAKE_BINARY_DIR}/coverage/coverage.info --exclude '/usr/*' --exclude '*/tests/*' --exclude '*/_deps/*'
        COMMAND ${LCOV_EXE} --list ${CMAKE_BINARY_DIR}/coverage/coverage.info
        COMMAND ${GENHTML_EXE} ${CMAKE_BINARY_DIR}/coverage/coverage.info --output-directory ${CMAKE_BINARY_DIR}/coverage/html
        DEPENDS agentware-tests
        COMMENT "Running tests and generating code coverage report"
    )
endif()

# Add preflight target (comprehensive pre-deployment checks)
add_custom_target(preflight
    COMMAND ${CMAKE_SOURCE_DIR}/src/scripts/preflight.sh
    DEPENDS agentware-agent
    COMMENT "Running comprehensive preflight checks"
)
