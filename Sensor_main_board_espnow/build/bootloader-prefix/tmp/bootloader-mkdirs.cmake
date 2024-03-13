# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/katomaran-emb/esp/esp-idf/components/bootloader/subproject"
  "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader"
  "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix"
  "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix/tmp"
  "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix/src/bootloader-stamp"
  "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix/src"
  "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/katomaran-emb/goat_1500kg_robot/Sensor_main_board_espnow/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
