include_guard(GLOBAL)

# =============================================================================
# Feature flag
# mapping Map CMake *ENABLE* variables to *HAS* compile definitions (1 or 0). definitions_list is
# the name of the list variable to append to (e.g. CORE_COMPILE_DEFINITIONS,
# MEMORY_COMPILE_DEFINITIONS).

function(compile_definition definitions_list enable_flag)
  string(REPLACE "ENABLE" "HAS" definition_name "${enable_flag}")
  if(NOT DEFINED ${enable_flag})
    set(_enabled OFF)
  else()
    set(_enabled ${${enable_flag}})
  endif()
  if(_enabled)
    set(_value 1)
  else()
    set(_value 0)
  endif()
  set(_current "${${definitions_list}}")
  list(APPEND _current "${definition_name}=${_value}")
  set(${definitions_list} "${_current}" PARENT_SCOPE)
endfunction()
