find_package(Qt5 5.15.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)

# Qt 5.15.2 predates the removal of the MSVC array iterator extensions.
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/qt5_msvc_compat.hpp>")
endif()

# Studio is an SDK consumer. Core can execute in the worker without Widgets.
add_library(mvkit_studio_core STATIC
  src/io/input_migration.cpp
  src/io/input_migration.hpp
  src/io/project_io.cpp
  src/io/project_io.hpp
  src/io/capture_resources.cpp
  src/io/delivery_report.cpp
  src/io/delivery_report.hpp
  src/io/model_template.cpp
  src/io/spatial_files.cpp
  src/model/sample_edit.cpp
  src/model/sample_edit.hpp
  src/model/project_state.hpp
  src/model/sample_inputs.cpp
  src/model/sample_inputs.hpp
  src/model/document.cpp
  src/model/document.hpp
  src/model/image_preview.cpp
  src/model/image_preview.hpp
  src/model/input_data.cpp
  src/model/workflow_ports.cpp
  src/model/workflow_ports.hpp
  src/model/metadata_limits.hpp
  src/model/spatial_data.hpp
  src/model/spatial_display.cpp
  src/operators/calibration_tools.cpp
  src/operators/calibration_tools.hpp
  src/operators/workflow_calibration.cpp
  src/operators/detection_tools.cpp
  src/operators/detection_tools.hpp
  src/operators/fitting_quality.cpp
  src/operators/fitting_quality.hpp
  src/operators/geometry_tools.cpp
  src/operators/image_processing.cpp
  src/operators/image_processing.hpp
  src/operators/operator_registry.cpp
  src/operators/operator_registry.hpp
  src/operators/range_model.cpp
  src/operators/range_model.hpp
  src/operators/spatial_data.cpp
  src/operators/structured_light.cpp
  src/runtime/execution.cpp
  src/runtime/execution.hpp
  src/runtime/run_controller.cpp
  src/runtime/run_controller.hpp
  src/runtime/run_snapshot.cpp
  src/runtime/run_snapshot.hpp
  src/runtime/repeated_execution.cpp
  src/runtime/worker_protocol.cpp
  src/runtime/worker_protocol.hpp
)
set_target_properties(mvkit_studio_core PROPERTIES AUTOMOC ON)
target_compile_features(mvkit_studio_core PUBLIC cxx_std_17)
target_include_directories(mvkit_studio_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mvkit_studio_core PUBLIC mvkit::CXX Qt5::Core Qt5::Gui)

add_library(mvkit_studio_ui STATIC
  src/ui/dialogs/resource_manager.cpp
  src/ui/dialogs/resource_manager.hpp
  src/ui/dialogs/sample_projects.cpp
  src/ui/dialogs/sample_projects.hpp
  src/ui/dialogs/sequence_view.cpp
  src/ui/dialogs/sequence_view.hpp
  src/ui/shell/calibration_ui.cpp
  src/ui/shell/capture_ui.cpp
  src/ui/shell/delivery_ui.cpp
  src/ui/shell/depth_scale_ui.cpp
  src/ui/shell/diagnostics_ui.cpp
  src/ui/shell/edit_history.cpp
  src/ui/shell/geometry_ui.cpp
  src/ui/shell/workflow_inputs_ui.cpp
  src/ui/shell/input_ui.cpp
  src/ui/shell/inspector_ui.cpp
  src/ui/shell/commands_ui.cpp
  src/ui/shell/project_ui.cpp
  src/ui/shell/loading_ui.cpp
  src/ui/shell/layout_state.cpp
  src/ui/shell/main_window.cpp
  src/ui/shell/main_window.hpp
  src/ui/shell/range_project.cpp
  src/ui/shell/range_ui.cpp
  src/ui/shell/spatial_delivery.cpp
  src/ui/shell/spatial_ui.cpp
  src/ui/shell/structured_ui.cpp
  src/ui/shell/view_debug.cpp
  src/ui/shell/workbench_context.cpp
  src/ui/style/appearance.cpp
  src/ui/style/appearance.hpp
  src/ui/style/studio_ui.qrc
  src/ui/style/operator_icons.cpp
  src/ui/style/operator_icons.hpp
  src/ui/style/window_chrome.cpp
  src/ui/style/window_chrome.hpp
  src/ui/style/fluent_paths.hpp
  src/ui/style/lucide_toolbar_paths.hpp
  src/ui/views/image_view.cpp
  src/ui/views/image_view.hpp
  src/ui/views/model_view.cpp
  src/ui/views/model_view.hpp
  src/ui/views/workflow_navigation.cpp
  src/ui/views/workflow_view.cpp
  src/ui/views/workflow_view.hpp
  src/ui/widgets/add_data_button.hpp
  src/ui/widgets/filmstrip_scroll.hpp
  src/ui/widgets/loading_task.hpp
  src/ui/widgets/node_view_dock.hpp
  src/ui/widgets/parameter_grid.hpp
  src/ui/widgets/run_selection_delegate.hpp
  src/ui/widgets/responsive_widgets.hpp
  src/ui/widgets/source_choice.hpp
  src/ui/widgets/frame_spin_box.hpp
  src/ui/widgets/studio_controls.hpp
)
set_target_properties(mvkit_studio_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(mvkit_studio_ui PUBLIC cxx_std_17)
target_link_libraries(mvkit_studio_ui PUBLIC mvkit_studio_core Qt5::Widgets)

add_executable(mvkit_studio_worker WIN32 src/worker_main.cpp)
target_link_libraries(mvkit_studio_worker PRIVATE mvkit_studio_core)
set_target_properties(mvkit_studio_worker PROPERTIES OUTPUT_NAME mv_studio_worker)
add_executable(mvkit_workbench WIN32 src/main.cpp)
add_dependencies(mvkit_workbench mvkit_studio_worker)
target_link_libraries(mvkit_workbench PRIVATE mvkit_studio_ui)
set_target_properties(mvkit_workbench PROPERTIES OUTPUT_NAME mv_studio)
target_compile_definitions(mvkit_workbench PRIVATE MVKIT_STUDIO_VERSION="${PROJECT_VERSION}")
# Share the release catalog between the build tree and runtime package.
set(mvkit_studio_bundles
  getting-started brightness image-calibration-review measurement-review
  second-batch-review third-batch-review stereo-review structured-review
  synthetic-stereo-part synthetic-structured-part)
add_custom_target(mvkit_studio_examples)
foreach(bundle IN LISTS mvkit_studio_bundles)
  add_custom_command(TARGET mvkit_studio_examples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${PROJECT_SOURCE_DIR}/examples/studio/${bundle}"
      "$<TARGET_FILE_DIR:mvkit_workbench>/studio-examples/${bundle}"
    VERBATIM)
endforeach()
add_custom_command(TARGET mvkit_studio_examples POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${PROJECT_SOURCE_DIR}/examples/studio/data/synthetic"
    "$<TARGET_FILE_DIR:mvkit_workbench>/studio-examples/data/synthetic"
  VERBATIM)
add_dependencies(mvkit_workbench mvkit_studio_examples)
add_custom_command(TARGET mvkit_workbench POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/third-party/qtbase-5.15.2" "$<TARGET_FILE_DIR:mvkit_workbench>/third-party/qtbase-5.15.2"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/third-party/fluentui-system-icons" "$<TARGET_FILE_DIR:mvkit_workbench>/third-party/fluentui-system-icons"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/third-party/lucide-icons" "$<TARGET_FILE_DIR:mvkit_workbench>/third-party/lucide-icons"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md" "$<TARGET_FILE_DIR:mvkit_workbench>/third-party/README.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "$<TARGET_FILE_DIR:mvkit_workbench>/WORKBENCH.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:mvkit_workbench>/LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/NOTICE" "$<TARGET_FILE_DIR:mvkit_workbench>/NOTICE"
  VERBATIM)
if(MSVC)
  target_compile_options(mvkit_studio_core PRIVATE /W4)
  target_compile_options(mvkit_studio_ui PRIVATE /W4)
endif()

if(WIN32)
  # All runtime files come from this build or the explicitly selected Qt SDK.
  get_target_property(qt_qmake Qt5::qmake IMPORTED_LOCATION)
  get_filename_component(qt_bin "${qt_qmake}" DIRECTORY)
  foreach(module Core Gui Widgets)
    add_custom_command(TARGET mvkit_workbench POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt5::${module}>" "$<TARGET_FILE_DIR:mvkit_workbench>" VERBATIM)
  endforeach()
  add_custom_command(TARGET mvkit_workbench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mvkit_workbench>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${qt_bin}/../plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
      "$<TARGET_FILE_DIR:mvkit_workbench>/platforms/" VERBATIM)
endif()

if(WIN32)
  include("${PROJECT_SOURCE_DIR}/cmake/StudioPackage.cmake")
endif()
