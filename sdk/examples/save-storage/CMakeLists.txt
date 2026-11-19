cmake_minimum_required(VERSION 3.20)
project(san14_save_storage LANGUAGES CXX)

set(SAN14_SDK_DIR "" CACHE PATH "Path to the SAN14 Mod SDK root (containing include/)")
if(NOT SAN14_SDK_DIR)
  if(DEFINED ENV{SAN14_SDK_DIR} AND EXISTS "$ENV{SAN14_SDK_DIR}/include/san14_mod.h")
    set(SAN14_SDK_DIR "$ENV{SAN14_SDK_DIR}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../include/san14_mod.h")
    get_filename_component(SAN14_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include/san14_mod.h")
    get_filename_component(SAN14_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" ABSOLUTE)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../include/san14_mod.h")
    get_filename_component(SAN14_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  else()
    message(FATAL_ERROR "Cannot locate SAN14 Mod SDK headers. Pass -DSAN14_SDK_DIR=<path-to-sdk>.")
  endif()
endif()

add_library(save_storage SHARED save_storage.cpp)
target_compile_features(save_storage PRIVATE cxx_std_17)
target_include_directories(save_storage PRIVATE "${SAN14_SDK_DIR}/include")
target_compile_definitions(save_storage PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(save_storage PROPERTIES
  PREFIX ""
  OUTPUT_NAME "save_storage"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/win-x64"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/win-x64"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin/win-x64"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin/win-x64"
)

if(MSVC)
  set_property(TARGET save_storage PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(save_storage PRIVATE /utf-8 /W4 /permissive- /EHsc)
endif()
