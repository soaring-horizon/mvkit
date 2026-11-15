# Windows runtime staging is opt-in and does not alter SDK install/export rules.
set(mvkit_studio_license_files)
foreach(dependency jpeg png tiff zlib)
  if(TARGET mvkit_dep_${dependency})
    if(dependency STREQUAL "jpeg")
      set(names LICENSE.md README.ijg)
    elseif(dependency STREQUAL "tiff")
      set(names LICENSE.md)
    else()
      set(names LICENSE README)
    endif()
    foreach(name IN LISTS names)
      list(APPEND mvkit_studio_license_files "${MVKIT_DEPENDENCY_ROOT}/${dependency}/${name}")
    endforeach()
  endif()
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/cmake/templates/StudioPackage.cmake.in"
  "${PROJECT_BINARY_DIR}/generated/StudioPackage.cmake" @ONLY)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/generated/StudioPackage-$<CONFIG>.cmake"
  INPUT "${PROJECT_BINARY_DIR}/generated/StudioPackage.cmake")
add_custom_target(mvkit_studio_package
  COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/generated/StudioPackage-$<CONFIG>.cmake"
  DEPENDS mvkit_workbench mvkit_studio_worker
  COMMENT "Staging the MV Studio runtime and release examples"
  VERBATIM)
