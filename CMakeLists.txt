cmake_minimum_required(VERSION 2.8)

project(guvcmjpg)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_BUILD_TYPE Release)

include(CompilerFlags)

find_package(SDL REQUIRED)
find_package(LibUSB REQUIRED)
find_package(udev REQUIRED)
find_package(PNG REQUIRED)
find_package(V4L2 REQUIRED)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libjpeg-turbo")

set(USE_PLANAR_YUV FALSE)

file(GLOB GVIEW_RENDER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/gview_render/*.c")
file(GLOB GVIEW_V4L2CORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/gview_v4l2core/*.c")
file(GLOB GUVCMJPG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/guvcmjpg/*.c")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(gview_render STATIC ${GVIEW_RENDER_SRC})
if (${USE_PLANAR_YUV})
target_compile_definitions(gview_render PRIVATE USE_PLANAR_YUV)
endif ()
target_include_directories(gview_render PRIVATE ${SDL_INCLUDE_DIR})

add_library(gview_v4l2core STATIC ${GVIEW_V4L2CORE_SRC})
if (${USE_PLANAR_YUV})
target_compile_definitions(gview_v4l2core PRIVATE USE_PLANAR_YUV)
endif()
target_include_directories(gview_v4l2core PRIVATE ${LibUSB_INCLUDE_DIRS})
target_include_directories(gview_v4l2core PRIVATE ${V4L2_INCLUDE_DIR})
target_include_directories(gview_v4l2core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libjpeg-turbo")

add_executable(guvcmjpg ${GUVCMJPG_SRC})
target_compile_definitions(guvcmjpg PRIVATE VERSION="2.0.2")
target_include_directories(guvcmjpg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/gview_render")
target_include_directories(guvcmjpg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/gview_v4l2core")
target_link_libraries(guvcmjpg gview_render gview_v4l2core m ${LibUSB_LIBRARIES} ${SDL_LIBRARY} ${UDEV_LIBRARIES} ${PNG_LIBRARIES} ${V4L2_LIBRARY} turbojpeg)

