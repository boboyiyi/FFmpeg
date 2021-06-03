cmake_minimum_required(VERSION 2.8)
project(learn_ffmpeg)

set(LOCAL_DIRECTORY $ENV{HOME}/workspace/local)

include_directories(${LOCAL_DIRECTORY}/include)

set(FFMPEG_LIBRARY)
set(FFMPEG_LIBRARY_NAMES "avcodec;avdevice;avfilter;avformat;avutil;postproc;swresample;swscale")
foreach(LIB_NAME ${FFMPEG_LIBRARY_NAMES})
    find_library(FOUND_LIB_${LIB_NAME} ${LIB_NAME} HINTS ${LOCAL_DIRECTORY}/lib)
    list(APPEND FFMPEG_LIBRARY ${FOUND_LIB_${LIB_NAME}})
endforeach()

add_executable(hello_world hello_world.c)
target_link_libraries(hello_world)