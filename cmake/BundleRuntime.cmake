# Called after copying Qt plugins, so their dependencies are included in the closure.
if(NOT WIN32 OR NOT DEFINED STAGE)
    message(FATAL_ERROR "Windows STAGE directory is required")
endif()
file(GLOB_RECURSE plugins "${STAGE}/*.dll")
file(TO_CMAKE_PATH "$ENV{PATH}" search_paths)
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${STAGE}/udp-vision.exe"
    LIBRARIES ${plugins}
    DIRECTORIES ${search_paths}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    # CMake can return mixed native/CMake separators (C:\Windows\system32/foo).
    POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss].*[Ss][Yy][Ss][Tt][Ee][Mm]32.*")
if(unresolved)
    message(FATAL_ERROR "Unresolved runtime libraries: ${unresolved}")
endif()
foreach(name IN LISTS conflicts_FILENAMES)
    set(previous_hash "")
    foreach(candidate IN LISTS conflicts_${name})
        file(SHA256 "${candidate}" hash)
        if(previous_hash AND NOT hash STREQUAL previous_hash)
            message(FATAL_ERROR "Different versions of ${name} in the toolchain; use matching Qt/OpenCV/compiler builds")
        endif()
        set(previous_hash "${hash}")
    endforeach()
    list(GET conflicts_${name} 0 candidate)
    list(APPEND resolved "${candidate}")
endforeach()
foreach(dll IN LISTS resolved)
    get_filename_component(parent "${dll}" DIRECTORY)
    if(NOT parent STREQUAL STAGE)
        file(COPY "${dll}" DESTINATION "${STAGE}")
    endif()
endforeach()
