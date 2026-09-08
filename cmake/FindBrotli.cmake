include(FindPackageHandleStandardArgs)
include(SelectLibraryConfigurations)

find_path(Brotli_INCLUDE_DIR
  NAMES brotli/decode.h
)

foreach(_brotli_component IN ITEMS DEC COMMON ENC)
  # A caller-supplied legacy path remains an override for all configurations.
  set(_brotli_override_${_brotli_component} FALSE)
  if(Brotli_${_brotli_component}_LIBRARY)
    set(_brotli_override_${_brotli_component} TRUE)
  else()
    string(TOLOWER "${_brotli_component}" _brotli_suffix)
    find_library(Brotli_${_brotli_component}_LIBRARY_RELEASE
      NAMES "brotli${_brotli_suffix}"
    )
    find_library(Brotli_${_brotli_component}_LIBRARY_DEBUG
      NAMES "brotli${_brotli_suffix}d"
    )
    select_library_configurations(Brotli_${_brotli_component})
  endif()
endforeach()

find_package_handle_standard_args(
  Brotli
  REQUIRED_VARS
    Brotli_INCLUDE_DIR
    Brotli_DEC_LIBRARY
    Brotli_COMMON_LIBRARY
)

if(Brotli_FOUND)
  set(_brotli_target_COMMON Brotli::common)
  set(_brotli_target_DEC Brotli::decoder)
  set(_brotli_target_ENC Brotli::encoder)
  foreach(_brotli_component IN ITEMS COMMON DEC ENC)
    set(_brotli_target "${_brotli_target_${_brotli_component}}")
    if(Brotli_${_brotli_component}_LIBRARY AND NOT TARGET ${_brotli_target})
      add_library(${_brotli_target} UNKNOWN IMPORTED)
      set_target_properties(${_brotli_target} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Brotli_INCLUDE_DIR}"
      )
      foreach(_brotli_config IN ITEMS RELEASE DEBUG)
        if(NOT _brotli_override_${_brotli_component}
           AND Brotli_${_brotli_component}_LIBRARY_${_brotli_config})
          set_property(TARGET ${_brotli_target} APPEND PROPERTY
            IMPORTED_CONFIGURATIONS ${_brotli_config})
          set_target_properties(${_brotli_target} PROPERTIES
            IMPORTED_LOCATION_${_brotli_config}
              "${Brotli_${_brotli_component}_LIBRARY_${_brotli_config}}"
          )
        endif()
      endforeach()
      # A prefix with one variant uses CMake's imported-configuration fallback.
      if(_brotli_override_${_brotli_component})
        set_target_properties(${_brotli_target} PROPERTIES
          IMPORTED_LOCATION "${Brotli_${_brotli_component}_LIBRARY}"
        )
      endif()
      if(NOT _brotli_component STREQUAL "COMMON")
        set_target_properties(${_brotli_target} PROPERTIES
          INTERFACE_LINK_LIBRARIES Brotli::common
        )
      endif()
    endif()
  endforeach()
endif()

mark_as_advanced(Brotli_INCLUDE_DIR Brotli_DEC_LIBRARY Brotli_COMMON_LIBRARY
  Brotli_ENC_LIBRARY)
unset(_brotli_component)
unset(_brotli_suffix)
unset(_brotli_config)
unset(_brotli_target)
unset(_brotli_target_COMMON)
unset(_brotli_target_DEC)
unset(_brotli_target_ENC)
unset(_brotli_override_COMMON)
unset(_brotli_override_DEC)
unset(_brotli_override_ENC)
