# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/ScreenshotLinux_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/ScreenshotLinux_autogen.dir/ParseCache.txt"
  "ScreenshotLinux_autogen"
  )
endif()
