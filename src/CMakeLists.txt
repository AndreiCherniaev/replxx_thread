cmake_minimum_required(VERSION 3.14)

project(replxx_thread LANGUAGES C CXX DESCRIPTION "test replxx with threads" VERSION 0.1.0.0)

add_definitions( -DMYPROJECT_NAME=${PROJECT_NAME} )
add_definitions( -DMYPROJECT_DESCRIPTION=${PROJECT_DESCRIPTION} )

include(FetchContent)
FetchContent_Declare(
  replxx
  GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx
  GIT_TAG        master
)
FetchContent_MakeAvailable(replxx)

set(CMAKE_AUTOMOC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_compile_definitions(_GNU_SOURCE)

qt_add_executable(${PROJECT_NAME}
#  cxx-api.cxx
  util.c util.h
  include/replxx.hxx

  ConsoleReader.h ConsoleReader.cpp
  MainClass.cpp MainClass.h
  main.cpp main.h
  ConsoleWorker.h ConsoleWorker.cpp
)

target_include_directories( ${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../mylibs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../mylibs/tmk_common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../mylibs/tmk_BC_common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../mylibs/BCN_specific
    ${CMAKE_CURRENT_SOURCE_DIR}/../../mylibs/Linux/include
)

target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core replxx::replxx)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION /usr/bin)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "27889022+AndreiCherniaev@users.noreply.github.com")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/AndreiCherniaev/replxx_thread")

set(CPACK_SET_DESTDIR true)
set(CPACK_INSTALL_PREFIX bin)

INCLUDE(CPack)
