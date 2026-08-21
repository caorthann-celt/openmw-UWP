set(_openmw_uwp_runtime_files
    "${OPENMW_UWP_RUNTIME_DIR}/SDL2.dll"
    "${OPENMW_UWP_RUNTIME_DIR}/opengl32.dll"
    "${OPENMW_UWP_RUNTIME_DIR}/libgallium_wgl.dll"
    "${OPENMW_UWP_RUNTIME_DIR}/libuwp.dll"
    "${OPENMW_UWP_RUNTIME_DIR}/dxil.dll"
    "${OPENMW_UWP_RUNTIME_DIR}/z-1.dll"
    "${OPENMW_UWP_RUNTIME_DIR}/lua51.dll"
)

foreach(_file ${_openmw_uwp_runtime_files})
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Missing UWP runtime file: ${_file}")
    endif()
endforeach()

set_source_files_properties(${_openmw_uwp_runtime_files} PROPERTIES
    VS_COPY_TO_OUT_DIR Always
    VS_DEPLOYMENT_CONTENT 1
    VS_DEPLOYMENT_LOCATION "."
)
target_sources(openmw PRIVATE ${_openmw_uwp_runtime_files})

file(GLOB _openmw_uwp_licenses CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ports/uwp/dependencies/lic/*")
list(APPEND _openmw_uwp_licenses "${CMAKE_SOURCE_DIR}/LICENSE")
set_source_files_properties(${_openmw_uwp_licenses} PROPERTIES
    VS_COPY_TO_OUT_DIR Always
    VS_DEPLOYMENT_CONTENT 1
    VS_DEPLOYMENT_LOCATION "licenses"
)
target_sources(openmw PRIVATE ${_openmw_uwp_licenses})

set(_openmw_uwp_manifest "${CMAKE_SOURCE_DIR}/ports/uwp/Package.appxmanifest")
target_sources(openmw PRIVATE "${_openmw_uwp_manifest}")
set_source_files_properties("${_openmw_uwp_manifest}" PROPERTIES
    VS_TOOL_OVERRIDE "AppxManifest"
    VS_COPY_TO_OUT_DIR Always
    VS_DEPLOYMENT_CONTENT 1
    VS_DEPLOYMENT_LOCATION "."
)

file(GLOB _openmw_uwp_assets CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ports/uwp/Assets/*.png")
foreach(_file ${_openmw_uwp_assets})
    set_source_files_properties("${_file}" PROPERTIES
        VS_TOOL_OVERRIDE "Content"
        VS_COPY_TO_OUT_DIR Always
        VS_DEPLOYMENT_CONTENT 1
        VS_DEPLOYMENT_LOCATION "Assets"
    )
endforeach()
target_sources(openmw PRIVATE ${_openmw_uwp_assets})

file(GLOB_RECURSE _openmw_uwp_launcher_assets CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/uwp/launcher/*.dds"
    "${CMAKE_SOURCE_DIR}/ports/uwp/launcher/*.jpg"
    "${CMAKE_SOURCE_DIR}/ports/uwp/launcher/*.png"
    "${CMAKE_SOURCE_DIR}/ports/uwp/launcher/*.ttf"
)
foreach(_file ${_openmw_uwp_launcher_assets})
    file(RELATIVE_PATH _relative "${CMAKE_SOURCE_DIR}/ports/uwp" "${_file}")
    get_filename_component(_location "${_relative}" DIRECTORY)
    set_source_files_properties("${_file}" PROPERTIES
        VS_TOOL_OVERRIDE "Content"
        VS_COPY_TO_OUT_DIR Always
        VS_DEPLOYMENT_CONTENT 1
        VS_DEPLOYMENT_LOCATION "${_location}"
    )
endforeach()
target_sources(openmw PRIVATE ${_openmw_uwp_launcher_assets})

foreach(_config ${CMAKE_CONFIGURATION_TYPES})
    set(_root "${OpenMW_BINARY_DIR}/${_config}")
    file(GLOB_RECURSE _resources "${_root}/resources/*")
    list(APPEND _resources
        "${_root}/resources/version"
        "${_root}/defaults.bin"
        "${_root}/gamecontrollerdb.txt"
        "${_root}/openmw.cfg"
    )
    list(REMOVE_DUPLICATES _resources)

    set_source_files_properties("${_root}/resources/version" PROPERTIES GENERATED TRUE)

    foreach(_file ${_resources})
        file(RELATIVE_PATH _relative "${_root}" "${_file}")
        get_filename_component(_location "${_relative}" DIRECTORY)
        if(_location STREQUAL "")
            set(_location ".")
        endif()

        set_source_files_properties("${_file}" PROPERTIES
            VS_COPY_TO_OUT_DIR Always
            VS_DEPLOYMENT_CONTENT 1
            VS_DEPLOYMENT_LOCATION "${_location}"
        )
        target_sources(openmw PRIVATE "$<$<CONFIG:${_config}>:${_file}>")
    endforeach()
endforeach()

if(TARGET get-version)
    add_dependencies(openmw get-version)
endif()
