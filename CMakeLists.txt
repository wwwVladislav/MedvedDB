# usage:
# cmake -DCMAKE_BUILD_TYPE=Release CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=Debug CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=MinSizeRel CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles" .

# mkdir build
# cd build>
# cmake -DCMAKE_INSTALL_PREFIX=/opt ..
# cpack ..

cmake_minimum_required(VERSION 3.10)
project(medveddb C)
cmake_policy(SET CMP0015 NEW)

#*********************************************************
# Options
#*********************************************************
option(BUILD_JNI "Build java native interface" ON)
option(ENABLE_SANITIZER "Enable address sanitizer" OFF)

include(cmake/sanitizer.cmake)

#*********************************************************
# Version
#*********************************************************
SET(MAJOR_VERSION 1)
SET(MINOR_VERSION 0)
SET(PATCH_VERSION 0)

set(VERSION ${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION})


#*********************************************************
# Paths to root and libraries directories
#*********************************************************
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mdv_thirdparty)


#*********************************************************
# set build type
#*********************************************************
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()


#*********************************************************
# determine platform
#*********************************************************
set(PLATFORM UNKNOWN)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(PLATFORM LINUX)
    if (DEFINED ${ANDROID_PLATFORM})
        set(PLATFORM ANDROID)
        add_definitions(-DANDROID_PLATFORM)
    endif()
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(PLATFORM MAC_OS)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(PLATFORM WINDOWS)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    set(PLATFORM ANDROID)
    add_definitions(-DANDROID_PLATFORM)
endif()

message("Platform: " ${CMAKE_SYSTEM_NAME} "-" ${PLATFORM})


#*********************************************************
# BITNESS
#*********************************************************
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BITNESS 64)
    add_definitions (-DMEDVED_X64)
    message("Bitness: 64 bit")
else()
    set(BITNESS 32)
    add_definitions (-DMEDVED_X86)
    message("Bitness: 32 bit")
endif()


#*********************************************************
# binary directory
#*********************************************************
if (NOT BIN_DIR)
    set(BIN_DIR ${CMAKE_CURRENT_BINARY_DIR})
endif()


#*********************************************************
# useful macros
#*********************************************************
macro(use_c11)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endmacro(use_c11)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)


#*********************************************************
# Libraries and executables
#*********************************************************
add_subdirectory(${ROOT_DIR}/mdv_thirdparty)
add_subdirectory(${ROOT_DIR}/mdv_storage)
add_subdirectory(${ROOT_DIR}/mdv_platform)
add_subdirectory(${ROOT_DIR}/mdv_crypto)
add_subdirectory(${ROOT_DIR}/mdv_net)
add_subdirectory(${ROOT_DIR}/mdv_types)
add_subdirectory(${ROOT_DIR}/mdv_core)
add_subdirectory(${ROOT_DIR}/mdv_service)
add_subdirectory(${ROOT_DIR}/mdv_api)
add_subdirectory(${ROOT_DIR}/mdv_bindings)
add_subdirectory(${ROOT_DIR}/mdv_client)
add_subdirectory(${ROOT_DIR}/mdv_tests)

#*********************************************************
# Documentation
#*********************************************************
include(assets/cmake/doxygen.cmake)

set(DOXY_INPUT
    ${ROOT_DIR}/mdv_api
    ${ROOT_DIR}/mdv_client
    ${ROOT_DIR}/mdv_core
    ${ROOT_DIR}/mdv_platform
    ${ROOT_DIR}/mdv_net
    ${ROOT_DIR}/mdv_service
    ${ROOT_DIR}/mdv_types
)

set(DOXY_OUTPUT_DIRECTORY "${BIN_DIR}/doc")

add_documentation(doc Doxyfile)


#*********************************************************
# CPack
#*********************************************************

if (${PLATFORM} STREQUAL LINUX)
    if(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
        message("CPack is found")
        include(InstallRequiredSystemLibraries)

        set(CPACK_SET_DESTDIR "on")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/tmp")
        set(CPACK_GENERATOR "DEB")

        set(CPACK_PACKAGE_DESCRIPTION "Medved DB")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MedvedDB is fully decentralized NoSQL database.")
        set(CPACK_PACKAGE_VENDOR "MedvedDB")
        set(CPACK_PACKAGE_CONTACT "wwwvladislav@gmail.com")
        set(CPACK_PACKAGE_VERSION_MAJOR "${MAJOR_VERSION}")
        set(CPACK_PACKAGE_VERSION_MINOR "${MINOR_VERSION}")
        set(CPACK_PACKAGE_VERSION_PATCH "${PATCH_VERSION}")
        set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${MAJOR_VERSION}.${MINOR_VERSION}.${CPACK_PACKAGE_VERSION_PATCH}")
        set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${MAJOR_VERSION}.${MINOR_VERSION}.${CPACK_PACKAGE_VERSION_PATCH}")

        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
        set(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})

        set(CPACK_INSTALL_PREFIX "/opt/medveddb")

        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/contrib/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/assets/contrib/deb/prerm;${CMAKE_CURRENT_SOURCE_DIR}/assets/contrib/deb/postrm;")

        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/contrib/etc/ld.so.conf.d/medveddb.conf      DESTINATION /etc/ld.so.conf.d)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/contrib/etc/systemd/system/medveddb.service DESTINATION /etc/systemd/system)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/conf/medved.conf                            DESTINATION /opt/medveddb)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/contrib/opt/medveddb/                   DESTINATION /opt/medveddb USE_SOURCE_PERMISSIONS)

        include(CPack)

    endif()
endif()
