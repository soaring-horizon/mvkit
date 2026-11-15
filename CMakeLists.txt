cmake_minimum_required(VERSION 3.20)
project(MVKit VERSION 0.2.0 LANGUAGES C CXX)

include(cmake/Configure.cmake)
include(cmake/Targets.cmake)
include(cmake/Install.cmake)

# 发布示例使用随包提供的 AI 工业图片。
option(MVKIT_BUILD_EXAMPLES "Build industrial image demos" OFF)
if(MVKIT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

option(MVKIT_BUILD_WORKBENCH "Build MV Studio with Qt 5.15.2" OFF)
if(MVKIT_BUILD_WORKBENCH)
  add_subdirectory(apps)
endif()
