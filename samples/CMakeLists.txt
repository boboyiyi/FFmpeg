cmake_minimum_required(VERSION 2.8)
SET(CMAKE_C_COMPILER /usr/bin/clang)
SET(CMAKE_CXX_COMPILER /usr/bin/clang++)
project(learn_ffmpeg)

# set(LOCAL_DIRECTORY $ENV{HOME}/workspace/local)
# 目前本地mac编译还有点问题，先用brew install ffmpeg这个版本的
set(LOCAL_DIRECTORY /usr/local)

include_directories(${LOCAL_DIRECTORY}/include)

# 如果某个包不能通过find_package寻找，以下是一个不错的方法
set(FFMPEG_LIBRARY)
set(FFMPEG_LIBRARY_NAMES "avcodec;avdevice;avfilter;avformat;avutil;postproc;swresample;swscale")
foreach(LIB_NAME ${FFMPEG_LIBRARY_NAMES})
    # 这里比较tricky，每一次找到的LIB名起不一样的名字，FOUND_LIB_${LIB_NAME}
    # 这样就不存在变量名cache的问题
    find_library(FOUND_LIB_${LIB_NAME} ${LIB_NAME} HINTS ${LOCAL_DIRECTORY}/lib)
    list(APPEND FFMPEG_LIBRARY ${FOUND_LIB_${LIB_NAME}})
endforeach()

add_executable(hello_world hello_world.c)
target_link_libraries(hello_world ${FFMPEG_LIBRARY})

add_executable(transmuxing transmuxing.c)
target_link_libraries(transmuxing ${FFMPEG_LIBRARY})