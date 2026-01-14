cmake_minimum_required(VERSION 3.22.1)
project(softph C)

# ------------------------------------------------------------------
# IMPORTANT: PJSIP dependency
# Place pjproject source tree at: app/src/main/cpp/pjproject
# Build it for Android (NDK) to produce libs under pjproject/pjsip-apps/bin and/or pjproject/lib.
#
# This CMake is configured to EXPECT prebuilt static libs in:
#   ${PJSIP_DIR}/lib
#
# If your pjproject build outputs elsewhere, update PJSIP_LIB_DIR below.
# ------------------------------------------------------------------

set(PJSIP_DIR "${CMAKE_CURRENT_LIST_DIR}/pjproject")
set(PJSIP_INCLUDE_DIR
        "${PJSIP_DIR}/pjlib/include"
        "${PJSIP_DIR}/pjlib-util/include"
        "${PJSIP_DIR}/pjnath/include"
        "${PJSIP_DIR}/pjsip/include"
        "${PJSIP_DIR}/pjmedia/include"
        "${PJSIP_DIR}/pjsip-apps/src/pjsua"
)

set(PJSIP_LIB_DIR "${PJSIP_DIR}/lib")

add_library(native-lib SHARED native-lib.c)

target_include_directories(native-lib PRIVATE ${PJSIP_INCLUDE_DIR})

add_library(pjsua STATIC IMPORTED)
set_target_properties(pjsua PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjsua-${ANDROID_ABI}.a")

add_library(pjsip-ua STATIC IMPORTED)
set_target_properties(pjsip-ua PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjsip-ua-${ANDROID_ABI}.a")

add_library(pjsip-simple STATIC IMPORTED)
set_target_properties(pjsip-simple PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjsip-simple-${ANDROID_ABI}.a")

add_library(pjsip STATIC IMPORTED)
set_target_properties(pjsip PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjsip-${ANDROID_ABI}.a")

add_library(pjmedia STATIC IMPORTED)
set_target_properties(pjmedia PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjmedia-${ANDROID_ABI}.a")

add_library(pjmedia-codec STATIC IMPORTED)
set_target_properties(pjmedia-codec PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjmedia-codec-${ANDROID_ABI}.a")

add_library(pjmedia-audiodev STATIC IMPORTED)
set_target_properties(pjmedia-audiodev PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjmedia-audiodev-${ANDROID_ABI}.a")

add_library(pjnath STATIC IMPORTED)
set_target_properties(pjnath PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjnath-${ANDROID_ABI}.a")

add_library(pjlib-util STATIC IMPORTED)
set_target_properties(pjlib-util PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpjlib-util-${ANDROID_ABI}.a")

add_library(pj STATIC IMPORTED)
set_target_properties(pj PROPERTIES IMPORTED_LOCATION "${PJSIP_LIB_DIR}/libpj-${ANDROID_ABI}.a")

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(native-lib
        pjsua
        pjsip-ua
        pjsip-simple
        pjsip
        pjmedia-codec
        pjmedia-audiodev
        pjmedia
        pjnath
        pjlib-util
        pj
        ${android-lib}
        ${log-lib}
)

