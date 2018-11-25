cmake_minimum_required(VERSION 3.12)

project(consistent_threadpool)

add_executable(${PROJECT_NAME}
  main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../include/thread_pool.h
)

if (UNIX)
  target_link_libraries(${PROJECT_NAME} pthread)
endif()
