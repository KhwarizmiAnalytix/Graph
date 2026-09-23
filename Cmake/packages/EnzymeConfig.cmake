# EnzymeConfig.cmake
# ------------------
# CMake package config for CONFIG-mode discovery (find_package(Enzyme CONFIG)). Delegates to
# FindEnzyme.cmake in this directory. Set Enzyme_DIR to this folder, e.g.: list(APPEND
# CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/Cmake/packages") or find_package(Enzyme CONFIG REQUIRED
# PATHS "${CMAKE_SOURCE_DIR}/Cmake/packages")

include("${CMAKE_CURRENT_LIST_DIR}/FindEnzyme.cmake")
