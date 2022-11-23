cmake_minimum_required(VERSION 3.0.0)

set(W2M_PROJECT_NAME
    Wav2Mp3
)
set(W2M_SOURCE_DIR
    src
)
set(W2M_SOURCE
    ${W2M_SOURCE_DIR}/wav2mp3.c
)
set(W2M_LIBRARIES_DIR
    libs
)
set(W2M_LIBRARIES 
    lame
)

project(${W2M_PROJECT_NAME})

#########
# GTest #
#########
#enable_testing()
#add_subdirectory(libs/gtest-1.7.0)
#include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

###########
# Project #
###########
include_directories("${W2M_LIBRARIES_DIR}/${W2M_LIBRARIES}")
link_directories("${W2M_LIBRARIES_DIR}/${W2M_LIBRARIES}")

add_executable(${W2M_PROJECT_NAME} ${W2M_SOURCE})

#foreach(LIBRARY ${W2M_LIBRARIES})
    #add_subdirectory("${W2M_LIBRARIES_DIR}/${LIBRARY}")
#endforeach(LIBRARY)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
if(WIN32) 
    message(STATUS ">>> Windows")
    target_link_libraries(${W2M_PROJECT_NAME} PRIVATE Threads::Threads libmp3lameWindows.a)
elseif(UNIX)
    message(STATUS ">>> UNIX")
    target_link_libraries(${W2M_PROJECT_NAME} PRIVATE Threads::Threads libmp3lame.a)
endif()
