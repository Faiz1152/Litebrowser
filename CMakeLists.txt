cmake_minimum_required(VERSION 3.19)
project(LiteBrowser)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Point this at the folder where you extracted the CEF SDK -------------
# That folder should contain "include/", "libcef_dll/", "Release/", "Resources/"
# Example: set(CEF_ROOT "C:/cef_binary_122.0.0_windows64")
if(NOT DEFINED CEF_ROOT)
  message(FATAL_ERROR "Set CEF_ROOT to the path of your CEF SDK folder, e.g. -DCEF_ROOT=C:/cef_binary_...")
endif()

get_filename_component(CEF_ROOT "${CEF_ROOT}" ABSOLUTE)

set(CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

add_subdirectory("${CEF_ROOT}/libcef_dll" libcef_dll_wrapper)

target_include_directories(libcef_dll_wrapper PUBLIC "${CEF_ROOT}")

add_executable(LiteBrowser WIN32
  src/main.cpp
  src/app.h
  src/client_handler.h
  src/client_handler.cpp
  resources/app.manifest
)

target_include_directories(LiteBrowser PRIVATE "${CEF_ROOT}")

target_compile_definitions(LiteBrowser PRIVATE 
    NOMINMAX 
    WIN32_LEAN_AND_MEAN 
    UNICODE 
    _UNICODE 
    NOIME 
    NOMCX 
    NOSERVICE 
    VC_EXTRA_LEAN 
    WIN32_EXTRA_LEAN
    "GetNextSibling=CefDOMNode_GetNextSibling_"
    "GetFirstChild=CefDOMNode_GetFirstChild_"
)

target_link_libraries(LiteBrowser
    libcef_lib 
    libcef_dll_wrapper 
    comctl32
    gdiplus
)

# Copy the CEF runtime files (the DLL + its data files) next to our exe
# automatically after each build, so you don't have to do it by hand.
add_custom_command(TARGET LiteBrowser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CEF_ROOT}/Release" "$<TARGET_FILE_DIR:LiteBrowser>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CEF_ROOT}/Resources" "$<TARGET_FILE_DIR:LiteBrowser>"
)
