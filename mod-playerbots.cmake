# mod-playerbots in its own static library, under MSVC, on a core that puts every static module in modules.lib.
#
# A COFF static library cannot exceed 4 GB (LNK1248). With mod-playerbots and the CoA modules, modules.lib reaches
# about 4.27 GB of the 4.29 GB allowed on the public Conquest-of-AzerothCore core, so the next additions would stop
# the link. modules/CMakeLists.txt includes this file right after it creates the modules target: the playerbots
# sources move to modules-playerbots, which modules links publicly, so worldserver gets both libraries. A core that
# already builds a modules-playerbots target is left as it is. Other compilers keep the stock layout.

if (MSVC AND TARGET modules AND NOT TARGET modules-playerbots)
  get_target_property(_coa_pb_all modules SOURCES)
  set(_coa_pb_sources)
  set(_coa_pb_others)
  foreach(_coa_pb_file IN LISTS _coa_pb_all)
    string(REPLACE "\\" "/" _coa_pb_path "${_coa_pb_file}")
    if (_coa_pb_path MATCHES "/modules/mod-playerbots/")
      list(APPEND _coa_pb_sources "${_coa_pb_file}")
    else()
      list(APPEND _coa_pb_others "${_coa_pb_file}")
    endif()
  endforeach()

  # A core that already spreads the module's .cpp files over libraries of its own (modules-part-N) leaves only
  # headers here: no library is made then, an add_library without any source would stop the configure.
  set(_coa_pb_cpp ${_coa_pb_sources})
  list(FILTER _coa_pb_cpp INCLUDE REGEX "[.](cpp|cc|cxx)$")
  if (_coa_pb_cpp)
    set_property(TARGET modules PROPERTY SOURCES ${_coa_pb_others})
    get_target_property(_coa_pb_includes modules INCLUDE_DIRECTORIES)
    get_target_property(_coa_pb_links modules LINK_LIBRARIES)

    add_library(modules-playerbots STATIC ${_coa_pb_sources})
    target_link_libraries(modules-playerbots
      PRIVATE
        ${_coa_pb_links})
    target_include_directories(modules-playerbots
      PRIVATE
        ${_coa_pb_includes})
    target_compile_options(modules-playerbots
      PRIVATE
        -DMOD_PLAYERBOTS
        $<TARGET_PROPERTY:modules,COMPILE_OPTIONS>)
    target_compile_definitions(modules-playerbots
      PRIVATE
        $<TARGET_PROPERTY:modules,COMPILE_DEFINITIONS>)
    set_target_properties(modules-playerbots PROPERTIES FOLDER "modules")
    target_link_libraries(modules PUBLIC modules-playerbots)

    list(LENGTH _coa_pb_sources _coa_pb_count)
    message(STATUS "mod-playerbots: ${_coa_pb_count} sources in their own static library (modules-playerbots, MSVC 4 GB limit)")
  endif()
endif()
