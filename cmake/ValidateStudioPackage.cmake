cmake_minimum_required(VERSION 3.20)
if(NOT IS_DIRECTORY "${MVKIT_STUDIO_PACKAGE}")
  message(FATAL_ERROR "Studio package directory is missing")
endif()
set(debug_suffix)
if(MVKIT_STUDIO_PACKAGE_CONFIG STREQUAL "Debug")
  set(debug_suffix d)
endif()
foreach(required mv_studio.exe mv_studio_worker.exe mvkit.dll mvkitcpp.dll
    Qt5Core${debug_suffix}.dll Qt5Gui${debug_suffix}.dll Qt5Widgets${debug_suffix}.dll
    platforms/qwindows${debug_suffix}.dll LICENSE NOTICE README.md
    third-party/qtbase-5.15.2/LICENSE.LGPL3
    studio-examples/getting-started/count.mvproj
    studio-examples/brightness/brightness.mvproj
    studio-examples/image-calibration-review/image-processing.mvproj
    studio-examples/measurement-review/measurement.mvproj
    studio-examples/second-batch-review/ellipse.mvproj
    studio-examples/third-batch-review/calibration.mvproj
    studio-examples/stereo-review/stereo.mvproj
    studio-examples/structured-review/structured.mvproj
    studio-examples/synthetic-stereo-part/synthetic-stereo-part.mvproj
    studio-examples/synthetic-structured-part/synthetic-structured-part.mvproj
    studio-examples/data/synthetic/optical-rig/truth.json
    studio-examples/data/synthetic/optical-rig/part/depth-0.1mm.png)
  if(NOT EXISTS "${MVKIT_STUDIO_PACKAGE}/${required}")
    message(FATAL_ERROR "Required Studio package file missing: ${required}")
  endif()
endforeach()
file(GLOB_RECURSE files LIST_DIRECTORIES FALSE RELATIVE "${MVKIT_STUDIO_PACKAGE}" "${MVKIT_STUDIO_PACKAGE}/*")
set(executables)
set(projects)
foreach(file IN LISTS files)
  string(TOLOWER "${file}" lower)
  if(lower MATCHES "[.]exe$")
    list(APPEND executables "${file}")
  endif()
  if(lower MATCHES "^studio-examples/.*[.]mvproj$")
    list(APPEND projects "${file}")
  endif()
  if(lower MATCHES "(^|/)(halcon[^/]*[.]dll|qoffscreen[^/]*[.]dll|real-[^/]+)(/|$)" OR lower MATCHES "(^|/)data/real(/|$)" OR lower MATCHES "[.](obj|pdb|lib|log)$")
    message(FATAL_ERROR "Development-only file in Studio package: ${file}")
  endif()
endforeach()
list(SORT executables)
set(expected mv_studio.exe mv_studio_worker.exe)
list(SORT expected)
if(NOT executables STREQUAL expected)
  message(FATAL_ERROR "Studio package must contain exactly the two runtime executables: ${executables}")
endif()
list(LENGTH projects project_count)
if(NOT project_count EQUAL 10)
  message(FATAL_ERROR "Studio package must contain exactly ten sample projects: ${projects}")
endif()
message(STATUS "Studio package contents verified: two executables and ten sample projects")
