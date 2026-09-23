# * Find INTEL MKL library
#
# This module sets the following variables: MKL_FOUND - set to true if a library implementing the
# CBLAS interface is found MKL_VERSION - best guess of the found mkl version MKL_INCLUDE_DIR - path
# to include dir. MKL_LIBRARIES - list of libraries for base mkl MKL_OPENMP_TYPE - OpenMP flavor
# that the found mkl uses: GNU or Intel MKL_OPENMP_LIBRARY - path to the OpenMP library the found
# mkl uses MKL_LAPACK_LIBRARIES - list of libraries to add for lapack MKL_SCALAPACK_LIBRARIES - list
# of libraries to add for scalapack MKL_SOLVER_LIBRARIES - list of libraries to add for the solvers
# MKL_CDFT_LIBRARIES - list of libraries to add for the solvers

# Do nothing if on ARM
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
  return()
endif()

# Do nothing if MKL_FOUND was set before!
if(NOT MKL_FOUND)

  set(MKL_VERSION)
  set(MKL_INCLUDE_DIR)
  set(MKL_LIBRARIES)
  set(MKL_OPENMP_TYPE)
  set(MKL_OPENMP_LIBRARY)
  set(MKL_LAPACK_LIBRARIES)
  set(MKL_SCALAPACK_LIBRARIES)
  set(MKL_SOLVER_LIBRARIES)
  set(MKL_CDFT_LIBRARIES)

  # Includes
  include(CheckTypeSize)
  include(CheckFunctionExists)

  # Set default value of INTEL_COMPILER_DIR and INTEL_MKL_DIR
  if(WIN32)
    if(DEFINED ENV{MKLProductDir})
      set(DEFAULT_INTEL_COMPILER_DIR $ENV{MKLProductDir})
    else()
      set(DEFAULT_INTEL_COMPILER_DIR
          "C:/Program Files (x86)/IntelSWTools/compilers_and_libraries/windows"
      )
    endif()
    set(DEFAULT_INTEL_MKL_DIR "${DEFAULT_INTEL_COMPILER_DIR}/mkl")
    if(EXISTS "${DEFAULT_INTEL_COMPILER_DIR}/mkl/latest")
      set(DEFAULT_INTEL_MKL_DIR "${DEFAULT_INTEL_COMPILER_DIR}/mkl/latest")
    endif()
  else(WIN32)
    set(DEFAULT_INTEL_COMPILER_DIR "/opt/intel")
    set(DEFAULT_INTEL_MKL_DIR "/opt/intel/mkl")
    set(DEFAULT_INTEL_ONEAPI_DIR "/opt/intel/oneapi")
    if(EXISTS "${DEFAULT_INTEL_ONEAPI_DIR}")
      set(DEFAULT_INTEL_COMPILER_DIR "${DEFAULT_INTEL_ONEAPI_DIR}")
      if(EXISTS "${DEFAULT_INTEL_ONEAPI_DIR}/compiler/latest")
        set(DEFAULT_INTEL_COMPILER_DIR "${DEFAULT_INTEL_ONEAPI_DIR}/compiler/latest")
      endif()
      if(EXISTS "${DEFAULT_INTEL_ONEAPI_DIR}/mkl/latest")
        set(DEFAULT_INTEL_MKL_DIR "${DEFAULT_INTEL_ONEAPI_DIR}/mkl/latest")
      endif()
    endif()
  endif(WIN32)

  # Intel Compiler Suite
  set(INTEL_COMPILER_DIR "${DEFAULT_INTEL_COMPILER_DIR}"
      CACHE STRING "Root directory of the Intel Compiler Suite (contains ipp, mkl, etc.)"
  )
  set(INTEL_MKL_DIR "${DEFAULT_INTEL_MKL_DIR}" CACHE STRING
                                                     "Root directory of the Intel MKL (standalone)"
  )
  set(INTEL_OMP_DIR "${DEFAULT_INTEL_MKL_DIR}"
      CACHE STRING "Root directory of the Intel OpenMP (standalone)"
  )
  set(MKL_THREADING "OMP" CACHE STRING "MKL flavor: SEQ, TBB or OMP (default)")

  if(NOT "${MKL_THREADING}" STREQUAL "SEQ" AND NOT "${MKL_THREADING}" STREQUAL "TBB"
     AND NOT "${MKL_THREADING}" STREQUAL "OMP"
  )
    message(FATAL_ERROR "Invalid MKL_THREADING (${MKL_THREADING}), should be one of: SEQ, TBB, OMP")
  endif()

  if("${MKL_THREADING}" STREQUAL "TBB" AND NOT TARGET TBB::tbb)
    message(FATAL_ERROR "MKL_THREADING is TBB but TBB is not found")
  endif()

  message(STATUS "MKL_THREADING = ${MKL_THREADING}")

  # Checks
  check_type_size("void*" SIZE_OF_VOIDP)
  if("${SIZE_OF_VOIDP}" EQUAL 8)
    set(mklvers "intel64")
    set(iccvers "intel64")
    set(mkl64s "_lp64")
  else("${SIZE_OF_VOIDP}" EQUAL 8)
    set(mklvers "32")
    set(iccvers "ia32")
    set(mkl64s)
  endif("${SIZE_OF_VOIDP}" EQUAL 8)

  if(WIN32)
    if("${MKL_THREADING}" STREQUAL "TBB")
      set(mklthreads "mkl_tbb_thread")
      if(CMAKE_BUILD_TYPE STREQUAL Debug)
        set(mklrtls "tbb12_debug")
      else()
        set(mklrtls "tbb12")
      endif()
    else()
      set(mklthreads "mkl_intel_thread")
      set(mklrtls "libiomp5md")
    endif()
    set(mklifaces "intel")
  else(WIN32)
    if(CMAKE_COMPILER_IS_GNUCC)
      if("${MKL_THREADING}" STREQUAL "TBB")
        set(mklthreads "mkl_tbb_thread")
        set(mklrtls "tbb")
      else()
        set(mklthreads "mkl_gnu_thread" "mkl_intel_thread")
        set(mklrtls "gomp" "iomp5")
      endif()
      set(mklifaces "intel" "gf")
    else(CMAKE_COMPILER_IS_GNUCC)
      if("${MKL_THREADING}" STREQUAL "TBB")
        set(mklthreads "mkl_tbb_thread")
        set(mklrtls "tbb")
      else()
        set(mklthreads "mkl_intel_thread")
        set(mklrtls "iomp5" "guide")
      endif()
      set(mklifaces "intel")
    endif(CMAKE_COMPILER_IS_GNUCC)
  endif(WIN32)

  # Kernel libraries dynamically loaded
  set(mklkerlibs
      "mc"
      "mc3"
      "nc"
      "p4n"
      "p4m"
      "p4m3"
      "p4p"
      "def"
  )
  set(mklseq)

  # Paths
  set(saved_CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH})
  set(saved_CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH})
  if(EXISTS ${INTEL_COMPILER_DIR})
    # TODO: diagnostic if dir does not exist
    set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_COMPILER_DIR}/lib/${iccvers}")
    if(MSVC)
      set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_COMPILER_DIR}/compiler/lib/${iccvers}")
    endif()
    if(APPLE)
      set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_COMPILER_DIR}/lib")
    endif()
    if(NOT EXISTS ${INTEL_MKL_DIR})
      set(INTEL_MKL_DIR "${INTEL_COMPILER_DIR}/mkl")
    endif()
  endif()
  if(EXISTS ${INTEL_MKL_DIR})
    # TODO: diagnostic if dir does not exist
    set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "${INTEL_MKL_DIR}/include")
    set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_MKL_DIR}/lib/${mklvers}")
    if(MSVC)
      set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_MKL_DIR}/lib/${iccvers}")
      if("${SIZE_OF_VOIDP}" EQUAL 8)
        set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_MKL_DIR}/win-x64")
      endif()
    endif()
    if(APPLE)
      set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_MKL_DIR}/lib")
    endif()
  endif()

  if(EXISTS ${INTEL_OMP_DIR})
    # TODO: diagnostic if dir does not exist
    set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "${INTEL_OMP_DIR}/include")
    set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_OMP_DIR}/lib/${mklvers}")
    if(MSVC)
      set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_OMP_DIR}/lib/${iccvers}")
      if("${SIZE_OF_VOIDP}" EQUAL 8)
        set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_OMP_DIR}/win-x64")
      endif()
    endif()
    if(APPLE)
      set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${INTEL_OMP_DIR}/lib"
                             "${INTEL_COMPILER_DIR}/mac/compiler/lib"
      )
    endif()
  endif()

  macro(GET_MKL_LIB_NAMES LIBRARIES INTERFACE MKL64)
    cmake_parse_arguments("" "" "THREAD" "" ${ARGN})
    set(${LIBRARIES} mkl_${INTERFACE}${MKL64} mkl_core)
    if(_THREAD)
      list(INSERT ${LIBRARIES} 1 ${_THREAD})
      if(UNIX AND ${ENABLE_STATIC_MKL})
        # The thread library defines symbols required by the other MKL libraries so also add it last
        list(APPEND ${LIBRARIES} ${_THREAD})
      endif()
    endif()
    if(${ENABLE_STATIC_MKL})
      if(UNIX)
        list(TRANSFORM ${LIBRARIES} PREPEND "lib")
        list(TRANSFORM ${LIBRARIES} APPEND ".a")
      else()
        message(WARNING "Ignoring ENABLE_STATIC_MKL")
      endif()
    endif()
  endmacro()

  # Try linking multiple libs
  macro(
    CHECK_ALL_LIBRARIES
    LIBRARIES
    OPENMP_TYPE
    OPENMP_LIBRARY
    _name
    _list
    _flags
  )
    # This macro checks for the existence of the combination of libraries given by _list. If the
    # combination is found, this macro checks whether we can link against that library combination
    # using the name of a routine given by _name using the linker flags given by _flags.  If the
    # combination of libraries is found and passes the link test, LIBRARIES is set to the list of
    # complete library paths that have been found.  Otherwise, LIBRARIES is set to FALSE. N.B.
    # _prefix is the prefix applied to the names of all cached variables that are generated
    # internally and marked advanced by this macro.
    set(_prefix "${LIBRARIES}")
    # start checking
    set(_libraries_work TRUE)
    set(${LIBRARIES})
    set(${OPENMP_TYPE})
    set(${OPENMP_LIBRARY})
    set(_combined_name)
    set(_openmp_type)
    set(_openmp_library)
    set(_paths)
    if(NOT MKL_FIND_QUIETLY)
      set(_str_list)
      foreach(_elem ${_list})
        if(_str_list)
          set(_str_list "${_str_list} - ${_elem}")
        else()
          set(_str_list "${_elem}")
        endif()
      endforeach(_elem)
      message(STATUS "Checking for [${_str_list}]")
    endif()
    set(_found_tbb FALSE)
    foreach(_library ${_list})
      set(_combined_name ${_combined_name}_${_library})
      unset(${_prefix}_${_library}_LIBRARY)
      if(_libraries_work)
        if(${_library} MATCHES "omp")
          if(_openmp_type)
            message(FATAL_ERROR "More than one OpenMP libraries appear in the MKL test: ${_list}")
          elseif(${_library} MATCHES "gomp")
            set(_openmp_type "GNU")
            # Use FindOpenMP to find gomp
            find_package(OpenMP QUIET)
            if(OPENMP_FOUND)
              # Test that none of the found library names contains "iomp" (Intel OpenMP). This
              # doesn't necessarily mean that we have gomp... but it is probably good enough since
              # on gcc we should already have OpenMP_CXX_FLAGS="-fopenmp" and
              # OpenMP_CXX_LIB_NAMES="".
              set(_found_gomp true)
              foreach(_lib_name ${OpenMP_CXX_LIB_NAMES})
                if(_found_gomp AND "${_lib_name}" MATCHES "iomp")
                  set(_found_gomp false)
                endif()
              endforeach()
              if(_found_gomp)
                set(${_prefix}_${_library}_LIBRARY ${OpenMP_CXX_FLAGS})
                set(_openmp_library "${${_prefix}_${_library}_LIBRARY}")
              endif()
            endif(OPENMP_FOUND)
          elseif(${_library} MATCHES "iomp")
            set(_openmp_type "Intel")
            find_library(${_prefix}_${_library}_LIBRARY NAMES ${_library})
            set(_openmp_library "${${_prefix}_${_library}_LIBRARY}")
          else()
            message(FATAL_ERROR "Unknown OpenMP flavor: ${_library}")
          endif()
        elseif(${_library} STREQUAL "tbb")
          # Separately handling compiled TBB
          set(_found_tbb TRUE)
        else()
          if(MSVC)
            set(lib_names ${_library}_dll)
            set(lib_names_static ${_library})
            # Both seek shared and static mkl library.
            find_library(${_prefix}_${_library}_LIBRARY NAMES ${lib_names} ${lib_names_static})
          else()
            set(lib_names ${_library})
            find_library(${_prefix}_${_library}_LIBRARY NAMES ${lib_names})
          endif()
        endif()
        mark_as_advanced(${_prefix}_${_library}_LIBRARY)
        if(NOT (${_library} STREQUAL "tbb"))
          set(${LIBRARIES} ${${LIBRARIES}} ${${_prefix}_${_library}_LIBRARY})
          set(_libraries_work ${${_prefix}_${_library}_LIBRARY})
          if(NOT MKL_FIND_QUIETLY)
            if(${_prefix}_${_library}_LIBRARY)
              message(STATUS "  Library ${_library}: ${${_prefix}_${_library}_LIBRARY}")
            else(${_prefix}_${_library}_LIBRARY)
              message(STATUS "  Library ${_library}: not found")
            endif(${_prefix}_${_library}_LIBRARY)
          endif()
        endif()
      endif(_libraries_work)
    endforeach(_library ${_list})
    # Test this combination of libraries.
    if(_libraries_work)
      if(NOT _found_tbb)
        set(CMAKE_REQUIRED_LIBRARIES ${_flags} ${${LIBRARIES}})
        set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES};${CMAKE_REQUIRED_LIBRARIES}")
        check_function_exists(${_name} ${_prefix}${_combined_name}_WORKS)
        set(CMAKE_REQUIRED_LIBRARIES)
        mark_as_advanced(${_prefix}${_combined_name}_WORKS)
        set(_libraries_work ${${_prefix}${_combined_name}_WORKS})
      endif()
    endif(_libraries_work)
    # Fin
    if(_libraries_work)
      set(${OPENMP_TYPE} ${_openmp_type})
      mark_as_advanced(${OPENMP_TYPE})
      set(${OPENMP_LIBRARY} ${_openmp_library})
      mark_as_advanced(${OPENMP_LIBRARY})
    else(_libraries_work)
      set(${LIBRARIES})
      mark_as_advanced(${LIBRARIES})
    endif(_libraries_work)
  endmacro(CHECK_ALL_LIBRARIES)

  if(WIN32)
    set(mkl_m "")
    set(mkl_pthread "")
  else(WIN32)
    set(mkl_m "m")
    set(mkl_pthread "pthread")
  endif(WIN32)

  if(UNIX AND NOT APPLE)
    set(mkl_dl "${CMAKE_DL_LIBS}")
  else(UNIX AND NOT APPLE)
    set(mkl_dl "")
  endif(UNIX AND NOT APPLE)

  # Check for version 10/11
  if(NOT MKL_LIBRARIES)
    set(MKL_VERSION 1011)
  endif(NOT MKL_LIBRARIES)

  # First: search for parallelized ones with intel thread lib
  if(NOT "${MKL_THREADING}" STREQUAL "SEQ")
    foreach(mklrtl ${mklrtls} "")
      foreach(mkliface ${mklifaces})
        foreach(mkl64 ${mkl64s} "")
          foreach(mklthread ${mklthreads})
            if(NOT MKL_LIBRARIES)
              get_mkl_lib_names(mkl_lib_names "${mkliface}" "${mkl64}" THREAD "${mklthread}")
              check_all_libraries(
                MKL_LIBRARIES MKL_OPENMP_TYPE MKL_OPENMP_LIBRARY cblas_sgemm
                "${mkl_lib_names};${mklrtl};${mkl_pthread};${mkl_m};${mkl_dl}" ""
              )
            endif(NOT MKL_LIBRARIES)
          endforeach(mklthread)
        endforeach(mkl64)
      endforeach(mkliface)
    endforeach(mklrtl)
  endif(NOT "${MKL_THREADING}" STREQUAL "SEQ")

  # Second: search for sequential ones
  foreach(mkliface ${mklifaces})
    foreach(mkl64 ${mkl64s} "")
      if(NOT MKL_LIBRARIES)
        get_mkl_lib_names(mkl_lib_names "${mkliface}" "${mkl64}" THREAD "mkl_sequential")
        check_all_libraries(
          MKL_LIBRARIES MKL_OPENMP_TYPE MKL_OPENMP_LIBRARY cblas_sgemm
          "${mkl_lib_names};${mkl_m};${mkl_dl}" ""
        )
        if(MKL_LIBRARIES)
          set(mklseq "_sequential")
        endif(MKL_LIBRARIES)
      endif(NOT MKL_LIBRARIES)
    endforeach(mkl64)
  endforeach(mkliface)

  # First: search for parallelized ones with native pthread lib
  foreach(mklrtl ${mklrtls} "")
    foreach(mkliface ${mklifaces})
      foreach(mkl64 ${mkl64s} "")
        if(NOT MKL_LIBRARIES)
          get_mkl_lib_names(mkl_lib_names "${mkliface}" "${mkl64}" THREAD "${mklthread}")
          check_all_libraries(
            MKL_LIBRARIES MKL_OPENMP_TYPE MKL_OPENMP_LIBRARY cblas_sgemm
            "${mkl_lib_names};${mklrtl};pthread;${mkl_m};${mkl_dl}" ""
          )
        endif(NOT MKL_LIBRARIES)
      endforeach(mkl64)
    endforeach(mkliface)
  endforeach(mklrtl)

  if(MKL_LIBRARIES)
    set(CMAKE_REQUIRED_LIBRARIES ${MKL_LIBRARIES})
    check_function_exists("cblas_gemm_bf16bf16f32" MKL_HAS_SBGEMM)
    check_function_exists("cblas_gemm_f16f16f32" MKL_HAS_SHGEMM)
    set(CMAKE_REQUIRED_LIBRARIES)
    if(MKL_HAS_SBGEMM)
      add_compile_options(-DMKL_HAS_SBGEMM)
    endif(MKL_HAS_SBGEMM)
    if(MKL_HAS_SHGEMM)
      add_compile_options(-DMKL_HAS_SHGEMM)
    endif(MKL_HAS_SHGEMM)
  endif(MKL_LIBRARIES)

  # Check for older versions
  if(NOT MKL_LIBRARIES)
    set(MKL_VERSION 900)
    if(ENABLE_STATIC_MKL)
      message(WARNING "Ignoring ENABLE_STATIC_MKL")
    endif()
    check_all_libraries(
      MKL_LIBRARIES MKL_OPENMP_TYPE MKL_OPENMP_LIBRARY cblas_sgemm "mkl;guide;pthread;m" ""
    )
  endif(NOT MKL_LIBRARIES)

  # Include files
  if(MKL_LIBRARIES)
    find_path(MKL_INCLUDE_DIR NAMES "mkl_cblas.h" PATHS "/usr/include/mkl")
    mark_as_advanced(MKL_INCLUDE_DIR)
  endif(MKL_LIBRARIES)

  # Other libraries
  if(MKL_LIBRARIES)
    foreach(mkl64 ${mkl64s} "_core" "")
      foreach(mkls ${mklseq} "")
        if(NOT MKL_LAPACK_LIBRARIES)
          find_library(MKL_LAPACK_LIBRARIES NAMES "mkl_lapack${mkl64}${mkls}")
          mark_as_advanced(MKL_LAPACK_LIBRARIES)
        endif(NOT MKL_LAPACK_LIBRARIES)
        if(NOT MKL_LAPACK_LIBRARIES)
          find_library(MKL_LAPACK_LIBRARIES NAMES "mkl_lapack95${mkl64}${mkls}")
          mark_as_advanced(MKL_LAPACK_LIBRARIES)
        endif(NOT MKL_LAPACK_LIBRARIES)
        if(NOT MKL_SCALAPACK_LIBRARIES)
          find_library(MKL_SCALAPACK_LIBRARIES NAMES "mkl_scalapack${mkl64}${mkls}")
          mark_as_advanced(MKL_SCALAPACK_LIBRARIES)
        endif(NOT MKL_SCALAPACK_LIBRARIES)
        if(NOT MKL_SOLVER_LIBRARIES)
          find_library(MKL_SOLVER_LIBRARIES NAMES "mkl_solver${mkl64}${mkls}")
          mark_as_advanced(MKL_SOLVER_LIBRARIES)
        endif(NOT MKL_SOLVER_LIBRARIES)
        if(NOT MKL_CDFT_LIBRARIES)
          find_library(MKL_CDFT_LIBRARIES NAMES "mkl_cdft${mkl64}${mkls}")
          mark_as_advanced(MKL_CDFT_LIBRARIES)
        endif(NOT MKL_CDFT_LIBRARIES)
      endforeach(mkls)
    endforeach(mkl64)
  endif(MKL_LIBRARIES)

  # Final
  set(CMAKE_LIBRARY_PATH ${saved_CMAKE_LIBRARY_PATH})
  set(CMAKE_INCLUDE_PATH ${saved_CMAKE_INCLUDE_PATH})
  if(MKL_LIBRARIES AND MKL_INCLUDE_DIR)
    set(MKL_FOUND TRUE)
  else(MKL_LIBRARIES AND MKL_INCLUDE_DIR)
    if(MKL_LIBRARIES AND NOT MKL_INCLUDE_DIR)
      message(
        WARNING
          "MKL libraries files are found, but MKL header files are \
      not. You can get them by `conda install mkl-include` if using conda (if \
      it is missing, run `conda upgrade -n root conda` first), and \
      `pip install mkl-devel` if using pip. If build fails with header files \
      available in the system, please make sure that CMake will search the \
      directory containing them, e.g., by setting CMAKE_INCLUDE_PATH."
      )
    endif()
    set(MKL_FOUND FALSE)
    set(MKL_VERSION) # clear MKL_VERSION
  endif(MKL_LIBRARIES AND MKL_INCLUDE_DIR)

  # Standard termination
  if(NOT MKL_FOUND AND MKL_FIND_REQUIRED)
    message(
      FATAL_ERROR
        "MKL library not found. Please specify library location \
    by appending the root directory of the MKL installation to the environment variable CMAKE_PREFIX_PATH."
    )
  endif(NOT MKL_FOUND AND MKL_FIND_REQUIRED)
  if(NOT MKL_FIND_QUIETLY)
    if(MKL_FOUND)
      message(STATUS "MKL library found")
    else(MKL_FOUND)
      message(STATUS "MKL library not found")
    endif(MKL_FOUND)
  endif(NOT MKL_FIND_QUIETLY)

  # Do nothing if MKL_FOUND was set before!
endif(NOT MKL_FOUND)
