include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageSource.cmake")

function(_arken_extpkg_patch_set_identity out_var recipe_json recipe_path recipe_id)
    _arken_extpkg_json_type(_patches_type "${recipe_json}" patches)
    if(_patches_type STREQUAL "")
        set(${out_var} "none" PARENT_SCOPE)
        return()
    endif()
    if(NOT _patches_type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.patch.array_invalid"
            "patch"
            "${recipe_id}"
            "patches must be an array."
        )
    endif()
    string(JSON _patch_count LENGTH "${recipe_json}" patches)
    if(_patch_count EQUAL 0)
        set(${out_var} "none" PARENT_SCOPE)
        return()
    endif()
    get_filename_component(_recipe_directory "${recipe_path}" DIRECTORY)
    set(_identity_input "")
    set(_patch_paths "")
    math(EXPR _last "${_patch_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _patch GET "${recipe_json}" patches ${_index})
        _arken_extpkg_validate_object_keys(
            "${_patch}"
            "${recipe_id}:patches[${_index}]"
            "path;sha256;strip"
        )
        _arken_extpkg_json_required_string(
            _relative
            "${_patch}"
            "${recipe_id}:patches[${_index}]"
            path
        )
        _arken_extpkg_json_required_string(
            _expected_sha
            "${_patch}"
            "${recipe_id}:patches[${_index}]"
            sha256
        )
        _arken_extpkg_json_optional_integer(
            _strip
            "${_patch}"
            1
            "${recipe_id}:patches[${_index}]"
            strip
        )
        string(LENGTH "${_expected_sha}" _sha_length)
        if(IS_ABSOLUTE "${_relative}"
                OR _relative MATCHES "(^|/)\\.\\.(/|$)"
                OR _relative MATCHES "[|;\n\r]"
                OR NOT _expected_sha MATCHES "^[0-9a-f]+$"
                OR NOT _sha_length EQUAL 64)
            _arken_extpkg_error(
                "arken.external_packages.patch.record_invalid"
                "patch"
                "${recipe_id}"
                "Patch records require a safe recipe-relative path, exact lowercase SHA-256, and non-negative strip count."
            )
        endif()
        set(_absolute "${_recipe_directory}/${_relative}")
        if(NOT EXISTS "${_absolute}" OR IS_DIRECTORY "${_absolute}" OR IS_SYMLINK "${_absolute}")
            _arken_extpkg_error(
                "arken.external_packages.patch.file_missing"
                "patch"
                "${recipe_id}"
                "Patch file is missing: ${_relative}"
            )
        endif()
        _arken_extpkg_add_configure_dependency("${_absolute}")
        file(SHA256 "${_absolute}" _observed_sha)
        if(NOT "${_observed_sha}" STREQUAL "${_expected_sha}")
            _arken_extpkg_error(
                "arken.external_packages.patch.hash_mismatch"
                "patch"
                "${recipe_id}"
                "Patch '${_relative}' expected SHA-256 ${_expected_sha}, observed ${_observed_sha}."
            )
        endif()
        list(FIND _patch_paths "${_relative}" _duplicate_index)
        if(NOT _duplicate_index EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.patch.duplicate"
                "patch"
                "${recipe_id}"
                "Patch path is declared more than once: ${_relative}"
            )
        endif()
        list(APPEND _patch_paths "${_relative}")
        string(APPEND _identity_input "${_relative}:${_expected_sha}:p${_strip}\n")
    endforeach()
    string(SHA256 _identity "${_identity_input}")
    set(${out_var} "${_identity}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_apply_patches
    out_source_root
    out_source_identity
    recipe_json
    recipe_path
    recipe_id
    base_source_root
    base_source_identity
    package_build_root
    patch_set_identity
)
    if("${patch_set_identity}" STREQUAL "none")
        set(${out_source_root} "${base_source_root}" PARENT_SCOPE)
        set(${out_source_identity} "${base_source_identity}" PARENT_SCOPE)
        return()
    endif()
    if(base_source_root STREQUAL "" OR NOT EXISTS "${base_source_root}")
        _arken_extpkg_error(
            "arken.external_packages.patch.source_missing"
            "patch"
            "${recipe_id}"
            "A non-empty patch set requires a materialized source tree."
        )
    endif()
    _arken_extpkg_json_required_string(
        _patch_tool_target
        "${recipe_json}"
        "${recipe_id}"
        adapter
        patchToolTarget
    )
    if(NOT TARGET "${_patch_tool_target}")
        _arken_extpkg_error(
            "arken.external_packages.patch.tool_missing"
            "patch"
            "${recipe_id}"
            "Patch tool target is unavailable: ${_patch_tool_target}"
        )
    endif()
    get_target_property(_patch_tool "${_patch_tool_target}" IMPORTED_LOCATION)
    get_target_property(_patch_tool_sha "${_patch_tool_target}" ARKEN_EXTERNAL_TOOL_SHA256)
    if(NOT _patch_tool
            OR NOT EXISTS "${_patch_tool}"
            OR NOT _patch_tool_sha MATCHES "^[0-9a-f]+$")
        _arken_extpkg_error(
            "arken.external_packages.patch.tool_invalid"
            "patch"
            "${recipe_id}"
            "Patch tool must be an identity-recorded imported executable."
        )
    endif()

    set(_patched_root "${package_build_root}/patched-source")
    set(_receipt "${package_build_root}/patched-source.receipt.json")
    if(EXISTS "${_receipt}")
        file(READ "${_receipt}" _receipt_json)
        string(JSON _receipt_patch_set ERROR_VARIABLE _patch_error GET "${_receipt_json}" patchSetSha256)
        string(JSON _receipt_base ERROR_VARIABLE _base_error GET "${_receipt_json}" baseSourceIdentity)
        string(JSON _receipt_tree ERROR_VARIABLE _tree_error GET "${_receipt_json}" patchedTreeSha256)
        if(_patch_error STREQUAL "NOTFOUND"
                AND _base_error STREQUAL "NOTFOUND"
                AND _tree_error STREQUAL "NOTFOUND"
                AND "${_receipt_patch_set}" STREQUAL "${patch_set_identity}"
                AND "${_receipt_base}" STREQUAL "${base_source_identity}")
            _arken_extpkg_tree_identity(_observed_tree "${_patched_root}" "${recipe_id}")
            if("${_observed_tree}" STREQUAL "${_receipt_tree}")
                set(${out_source_root} "${_patched_root}" PARENT_SCOPE)
                set(
                    ${out_source_identity}
                    "${base_source_identity}|patch-set:${patch_set_identity}|patched-tree:${_receipt_tree}|patch-tool:${_patch_tool_sha}"
                    PARENT_SCOPE
                )
                return()
            endif()
        endif()
        file(REMOVE_RECURSE "${_patched_root}")
        file(REMOVE "${_receipt}")
    endif()

    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _nonce)
    set(_temporary "${_patched_root}.partial-${_nonce}")
    file(REMOVE_RECURSE "${_temporary}")
    file(MAKE_DIRECTORY "${_temporary}")
    file(COPY "${base_source_root}/" DESTINATION "${_temporary}")
    get_filename_component(_recipe_directory "${recipe_path}" DIRECTORY)
    string(JSON _patch_count LENGTH "${recipe_json}" patches)
    math(EXPR _last "${_patch_count} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _patch GET "${recipe_json}" patches ${_index})
        _arken_extpkg_json_required_string(
            _relative
            "${_patch}"
            "${recipe_id}:patches[${_index}]"
            path
        )
        _arken_extpkg_json_optional_integer(
            _strip
            "${_patch}"
            1
            "${recipe_id}:patches[${_index}]"
            strip
        )
        set(_absolute "${_recipe_directory}/${_relative}")
        execute_process(
            COMMAND
                "${_patch_tool}"
                -C
                -f
                -s
                "-p${_strip}"
                -i
                "${_absolute}"
            WORKING_DIRECTORY "${_temporary}"
            RESULT_VARIABLE _check_result
            OUTPUT_VARIABLE _check_output
            ERROR_VARIABLE _check_error
        )
        if(NOT _check_result EQUAL 0)
            file(REMOVE_RECURSE "${_temporary}")
            _arken_extpkg_error(
                "arken.external_packages.patch.check_failed"
                "patch"
                "${recipe_id}"
                "Patch '${_relative}' failed preflight: ${_check_output}${_check_error}"
            )
        endif()
        execute_process(
            COMMAND
                "${_patch_tool}"
                -f
                -s
                "-p${_strip}"
                -i
                "${_absolute}"
            WORKING_DIRECTORY "${_temporary}"
            RESULT_VARIABLE _apply_result
            OUTPUT_VARIABLE _apply_output
            ERROR_VARIABLE _apply_error
        )
        if(NOT _apply_result EQUAL 0)
            file(REMOVE_RECURSE "${_temporary}")
            _arken_extpkg_error(
                "arken.external_packages.patch.apply_failed"
                "patch"
                "${recipe_id}"
                "Patch '${_relative}' failed after successful preflight: ${_apply_output}${_apply_error}"
            )
        endif()
    endforeach()
    _arken_extpkg_tree_identity(_patched_tree "${_temporary}" "${recipe_id}")
    _arken_extpkg_json_escape(_base_identity_json "${base_source_identity}")
    string(CONCAT _receipt_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"recipeId\": \"${recipe_id}\",\n"
        "  \"baseSourceIdentity\": \"${_base_identity_json}\",\n"
        "  \"patchSetSha256\": \"${patch_set_identity}\",\n"
        "  \"patchToolSha256\": \"${_patch_tool_sha}\",\n"
        "  \"patchedTreeSha256\": \"${_patched_tree}\"\n"
        "}\n"
    )
    file(RENAME "${_temporary}" "${_patched_root}" RESULT _rename_result)
    if(NOT "${_rename_result}" STREQUAL "0")
        file(REMOVE_RECURSE "${_temporary}")
        _arken_extpkg_error(
            "arken.external_packages.patch.publish_failed"
            "patch"
            "${recipe_id}"
            "Patched source publication failed: ${_rename_result}"
        )
    endif()
    _arken_extpkg_write_if_different("${_receipt}" "${_receipt_json}")
    set(${out_source_root} "${_patched_root}" PARENT_SCOPE)
    set(
        ${out_source_identity}
        "${base_source_identity}|patch-set:${patch_set_identity}|patched-tree:${_patched_tree}|patch-tool:${_patch_tool_sha}"
        PARENT_SCOPE
    )
endfunction()
