#
# Copyright (C) 2016 Emmanuel Durand
#
# This file is part of Splash.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Splash is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Splash.  If not, see <http://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.2)
project(
    splash 
    VERSION 0.3.13
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

#
# Package information
#
set(API_VERSION 0.3)
set(PACKAGE_VERSION 0.3.13)

#
# Find the various libraries
#
find_package(PkgConfig REQUIRED)

# Mandatory dependencies
find_package(Snappy REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
pkg_search_module(GSL REQUIRED gsl)
pkg_search_module(ZMQ REQUIRED libzmq)

# Optional dependencies
find_package(Boost COMPONENTS system thread)
pkg_search_module(GPHOTO libgphoto2)
pkg_check_modules(FFMPEG libavformat libavcodec libavutil libswscale)
pkg_search_module(OPENCV opencv)
pkg_search_module(PORTAUDIO portaudio-2.0)
pkg_search_module(SHMDATA shmdata-1.2)

#
# Configuration
#
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGLM_FORCE_RADIANS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDATADIR=\\\"${CMAKE_INSTALL_PREFIX}/share/splash/\\\"")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSPLASHPREFIX=\\\"${CMAKE_INSTALL_PREFIX}\\\"")

set(HAVE_GPHOTO ${GPHOTO_FOUND})
set(HAVE_FFMPEG ${FFMPEG_FOUND})
set(HAVE_OPENCV ${OPENCV_FOUND})
set(HAVE_PORTAUDIO ${PORTAUDIO_FOUND})
set(HAVE_SHMDATA ${SHMDATA_FOUND})

if (HAVE_FFMPEG AND FFMPEG_libavformat_VERSION GREATER 57)
    set(HAVE_FFMPEG_3 1)
else()
    set(HAVE_FFMPEG_3 0)
endif()

if(UNIX AND NOT APPLE)
    set(HAVE_LINUX 1)
    set(HAVE_OSX 0)
elseif(APPLE)
    set(HAVE_LINUX 0)
    set(HAVE_OSX 1)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/config.h.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/include/config.h")

#
# Sources
#
add_subdirectory(addons)
add_subdirectory(external)
add_subdirectory(data)
add_subdirectory(src)

#
# Summary
#

set(_config_msg "\nSplash Configuration\n=====================")

function(info_cfg_option
    _setting
    )
    
    set(_msg "  - ${_setting}")
    string(LENGTH "${_msg}" _len)
    while("32" GREATER "${_len}")
        set(_msg "${_msg} ")
         math(EXPR _len "${_len} + 1")
    endwhile()
    
    set(_config_msg "${_config_msg}\n${_msg}${${_setting}}" PARENT_SCOPE)
endfunction()

function(info_cfg_text
    _text
    )
    
    set(_config_msg "${_config_msg}\n\n  ${_text}" PARENT_SCOPE)
endfunction()

info_cfg_option(Boost_VERSION)
info_cfg_option(GPHOTO_VERSION)
info_cfg_option(FFMPEG_libavformat_VERSION)
info_cfg_option(FFMPEG_libavcodec_VERSION)
info_cfg_option(FFMPEG_libavutil_VERSION)
info_cfg_option(FFMPEG_libswscale_VERSION)
info_cfg_option(HAVE_FFMPEG_3)
info_cfg_option(OPENCV_VERSION)
info_cfg_option(PORTAUDIO_VERSION)
info_cfg_option(SHMDATA_VERSION)

info_cfg_text("")
message("${_config_msg}")