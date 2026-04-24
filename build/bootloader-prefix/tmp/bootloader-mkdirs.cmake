# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/rushil/.espressif/v6.0/esp-idf/components/bootloader/subproject"
  "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader"
  "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix"
  "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix/tmp"
  "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix/src/bootloader-stamp"
  "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix/src"
  "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/rushil/Documents/Temperature Project/3D-Printer-Room-Temperature-Monitor-/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
