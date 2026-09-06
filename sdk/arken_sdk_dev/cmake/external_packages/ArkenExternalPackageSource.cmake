include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")

function(_arken_extpkg_resolve_declared_path out_var root_kind relative_path recipe_path workspace_root)
    if(root_kind STREQUAL "workspace")
        set(_root "${workspace_root}")
    elseif(root_kind STREQUAL "sdk")
        get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    elseif(root_kind STREQUAL "recipe")
        get_filename_component(_root "${recipe_path}" DIRECTORY)
    elseif(root_kind STREQUAL "absolute")
        set(_root "")
    else()
        _arken_extpkg_error(
            "arken.external_packages.source.root_invalid"
            "source"
            "${recipe_path}"
            "Unsupported source root '${root_kind}'. Expected workspace, sdk, recipe, or absolute."
        )
    endif()
    if(root_kind STREQUAL "absolute")
        if(NOT IS_ABSOLUTE "${relative_path}")
            _arken_extpkg_error(
                "arken.external_packages.source.absolute_path_required"
                "source"
                "${recipe_path}"
                "Source root 'absolute' requires an absolute path."
            )
        endif()
        cmake_path(NORMAL_PATH relative_path OUTPUT_VARIABLE _resolved)
    else()
        cmake_path(ABSOLUTE_PATH relative_path BASE_DIRECTORY "${_root}" NORMALIZE OUTPUT_VARIABLE _resolved)
    endif()
    set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_tree_identity out_var root subject)
    if(NOT EXISTS "${root}")
        _arken_extpkg_error(
            "arken.external_packages.source.missing"
            "source"
            "${subject}"
            "Source root does not exist: ${root}"
        )
    endif()
    file(GLOB_RECURSE _entries LIST_DIRECTORIES FALSE RELATIVE "${root}" "${root}/*")
    list(SORT _entries)
    set(_identity_input "")
    set(_identity_chunk "")
    set(_identity_chunk_entries 0)
    foreach(_entry IN LISTS _entries)
        set(_absolute "${root}/${_entry}")
        if(IS_SYMLINK "${_absolute}")
            file(READ_SYMLINK "${_absolute}" _link_target)
            string(APPEND _identity_chunk "symlink:${_entry}:${_link_target}\n")
        else()
            file(SHA256 "${_absolute}" _digest)
            file(SIZE "${_absolute}" _size)
            string(APPEND _identity_chunk "file:${_entry}:${_size}:${_digest}\n")
        endif()
        # CMake copies string values on append. Bound the per-file copy cost
        # for LLVM-sized source inventories without changing a single byte of
        # the sorted tree identity, including symlinks and the final newline.
        math(EXPR _identity_chunk_entries "${_identity_chunk_entries} + 1")
        if(_identity_chunk_entries EQUAL 256)
            string(APPEND _identity_input "${_identity_chunk}")
            set(_identity_chunk "")
            set(_identity_chunk_entries 0)
        endif()
    endforeach()
    string(APPEND _identity_input "${_identity_chunk}")
    string(SHA256 _tree_sha "${_identity_input}")
    set(${out_var} "${_tree_sha}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_verify_declared_files recipe_json source_root recipe_id)
    _arken_extpkg_json_type(_files_type "${recipe_json}" source files)
    if(_files_type STREQUAL "")
        return()
    endif()
    if(NOT _files_type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.source.files_invalid"
            "source"
            "${recipe_id}"
            "source.files must be an array."
        )
    endif()
    string(JSON _length LENGTH "${recipe_json}" source files)
    if(_length EQUAL 0)
        return()
    endif()
    math(EXPR _last "${_length} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _record GET "${recipe_json}" source files ${_index})
        _arken_extpkg_validate_object_keys(
            "${_record}"
            "${recipe_id}:source.files[${_index}]"
            "path;sha256"
        )
        _arken_extpkg_json_required_string(_relative "${_record}" "${recipe_id}" path)
        _arken_extpkg_json_required_string(_expected "${_record}" "${recipe_id}" sha256)
        if(_relative MATCHES "(^|/)\\.\\.(/|$)" OR IS_ABSOLUTE "${_relative}")
            _arken_extpkg_error(
                "arken.external_packages.source.file_path_unsafe"
                "source"
                "${recipe_id}"
                "Declared source file path is unsafe: ${_relative}"
            )
        endif()
        set(_absolute "${source_root}/${_relative}")
        if(NOT EXISTS "${_absolute}" OR IS_DIRECTORY "${_absolute}" OR IS_SYMLINK "${_absolute}")
            _arken_extpkg_error(
                "arken.external_packages.source.file_missing"
                "source"
                "${recipe_id}"
                "Declared source file is missing or not a regular file: ${_relative}"
            )
        endif()
        _arken_extpkg_add_configure_dependency("${_absolute}")
        file(SHA256 "${_absolute}" _observed)
        if(NOT "${_observed}" STREQUAL "${_expected}")
            _arken_extpkg_error(
                "arken.external_packages.source.file_hash_mismatch"
                "source"
                "${recipe_id}"
                "File '${_relative}' expected SHA-256 ${_expected}, observed ${_observed}."
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_verify_expected_source_tree recipe_json source_root recipe_id)
    _arken_extpkg_json_optional_string(
        _expected_tree
        "${recipe_json}"
        ""
        "${recipe_id}"
        source
        treeSha256
    )
    if(_expected_tree STREQUAL "")
        return()
    endif()
    _arken_extpkg_tree_identity(_observed_tree "${source_root}" "${recipe_id}")
    if(NOT "${_observed_tree}" STREQUAL "${_expected_tree}")
        _arken_extpkg_error(
            "arken.external_packages.source.tree_hash_mismatch"
            "source"
            "${recipe_id}"
            "Expected source tree SHA-256 ${_expected_tree}, observed ${_observed_tree}."
        )
    endif()
endfunction()

function(_arken_extpkg_archive_link_exclusions
    out_paths
    out_targets
    recipe_json
    recipe_id
)
    _arken_extpkg_json_type(
        _exclusions_type
        "${recipe_json}"
        source
        excludedArchiveLinks
    )
    if(_exclusions_type STREQUAL "")
        set(${out_paths} "" PARENT_SCOPE)
        set(${out_targets} "" PARENT_SCOPE)
        return()
    endif()
    if(NOT _exclusions_type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.archive.exclusions_invalid"
            "schema"
            "${recipe_id}"
            "source.excludedArchiveLinks must be an array."
        )
    endif()
    string(
        JSON
        _exclusion_count
        LENGTH
        "${recipe_json}"
        source
        excludedArchiveLinks
    )
    set(_paths "")
    set(_targets "")
    if(_exclusion_count GREATER 0)
        math(EXPR _exclusion_last "${_exclusion_count} - 1")
        foreach(_index RANGE 0 ${_exclusion_last})
            string(
                JSON
                _exclusion
                GET
                "${recipe_json}"
                source
                excludedArchiveLinks
                ${_index}
            )
            _arken_extpkg_validate_object_keys(
                "${_exclusion}"
                "${recipe_id}:source.excludedArchiveLinks[${_index}]"
                "path;target;reason"
            )
            _arken_extpkg_json_required_string(
                _path
                "${_exclusion}"
                "${recipe_id}:source.excludedArchiveLinks[${_index}]"
                path
            )
            _arken_extpkg_json_required_string(
                _target
                "${_exclusion}"
                "${recipe_id}:source.excludedArchiveLinks[${_index}]"
                target
            )
            _arken_extpkg_json_required_string(
                _reason
                "${_exclusion}"
                "${recipe_id}:source.excludedArchiveLinks[${_index}]"
                reason
            )
            if(IS_ABSOLUTE "${_path}"
                    OR _path MATCHES "^[A-Za-z]:[/\\\\]"
                    OR _path MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)"
                    OR _path MATCHES "[;\n\r]")
                _arken_extpkg_error(
                    "arken.external_packages.archive.exclusion_path_unsafe"
                    "schema"
                    "${recipe_id}"
                    "Excluded archive link path is unsafe: ${_path}"
                )
            endif()
            if(_target MATCHES "[;\n\r]")
                _arken_extpkg_error(
                    "arken.external_packages.archive.exclusion_target_unsafe"
                    "schema"
                    "${recipe_id}"
                    "Excluded archive link target contains an unsafe list or "
                    "line delimiter: ${_target}"
                )
            endif()
            list(FIND _paths "${_path}" _duplicate_index)
            if(NOT _duplicate_index EQUAL -1)
                _arken_extpkg_error(
                    "arken.external_packages.archive.exclusion_duplicate"
                    "schema"
                    "${recipe_id}"
                    "Excluded archive link path is duplicated: ${_path}"
                )
            endif()
            list(APPEND _paths "${_path}")
            list(APPEND _targets "${_target}")
        endforeach()
    endif()
    set(${out_paths} "${_paths}" PARENT_SCOPE)
    set(${out_targets} "${_targets}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_validate_archive_listing
    archive
    recipe_json
    recipe_id
    allow_safe_symlinks
)
    _arken_extpkg_archive_link_exclusions(
        _excluded_paths
        _excluded_targets
        "${recipe_json}"
        "${recipe_id}"
    )
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _listing
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0)
        _arken_extpkg_error(
            "arken.external_packages.archive.list_failed"
            "acquisition"
            "${recipe_id}"
            "Archive listing failed: ${_error}"
        )
    endif()
    string(REPLACE "\r\n" "\n" _listing "${_listing}")
    string(REPLACE "\n" ";" _entries "${_listing}")
    foreach(_entry IN LISTS _entries)
        if(_entry STREQUAL "")
            continue()
        endif()
        if(IS_ABSOLUTE "${_entry}"
                OR _entry MATCHES "^[A-Za-z]:[/\\\\]"
                OR _entry MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)")
            _arken_extpkg_error(
                "arken.external_packages.archive.path_unsafe"
                "extraction"
                "${recipe_id}"
                "Archive entry escapes the extraction root: ${_entry}"
            )
        endif()
    endforeach()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar tvf "${archive}"
        RESULT_VARIABLE _verbose_result
        OUTPUT_VARIABLE _verbose_listing
        ERROR_VARIABLE _verbose_error
    )
    if(NOT _verbose_result EQUAL 0)
        _arken_extpkg_error(
            "arken.external_packages.archive.verbose_list_failed"
            "acquisition"
            "${recipe_id}"
            "Archive link inspection failed: ${_verbose_error}"
        )
    endif()
    string(REPLACE "\r\n" "\n" _verbose_listing "${_verbose_listing}")
    string(REPLACE "\n" ";" _verbose_entries "${_verbose_listing}")
    set(_observed_excluded_paths "")
    foreach(_verbose_entry IN LISTS _verbose_entries)
        if(NOT _verbose_entry MATCHES "^[lh]")
            continue()
        endif()
        if(NOT allow_safe_symlinks)
            _arken_extpkg_error(
                "arken.external_packages.archive.symlink_rejected"
                "extraction"
                "${recipe_id}"
                "Archive contains a symbolic or hard link, but the recipe does not admit links."
            )
        endif()
        set(_link_target "")
        set(_link_path "")
        if(_verbose_entry MATCHES " -> (.*)$")
            set(_link_target "${CMAKE_MATCH_1}")
            if(_verbose_entry MATCHES " ([^ ]+) -> ")
                set(_link_path "${CMAKE_MATCH_1}")
            endif()
        elseif(_verbose_entry MATCHES " link to (.*)$")
            set(_link_target "${CMAKE_MATCH_1}")
            if(_verbose_entry MATCHES " ([^ ]+) link to ")
                set(_link_path "${CMAKE_MATCH_1}")
            endif()
        endif()
        list(FIND _excluded_paths "${_link_path}" _excluded_index)
        if(NOT _excluded_index EQUAL -1)
            list(GET _excluded_targets ${_excluded_index}
                _expected_excluded_target)
            if(NOT "${_link_target}" STREQUAL
                    "${_expected_excluded_target}")
                _arken_extpkg_error(
                    "arken.external_packages.archive.exclusion_target_mismatch"
                    "extraction"
                    "${recipe_id}"
                    "Excluded archive link ${_link_path} expected target "
                    "${_expected_excluded_target}, observed ${_link_target}."
                )
            endif()
            list(FIND _observed_excluded_paths "${_link_path}"
                _duplicate_observed_index)
            if(NOT _duplicate_observed_index EQUAL -1)
                _arken_extpkg_error(
                    "arken.external_packages.archive.exclusion_duplicate_entry"
                    "extraction"
                    "${recipe_id}"
                    "Archive contains duplicate excluded link entries: "
                    "${_link_path}"
                )
            endif()
            list(APPEND _observed_excluded_paths "${_link_path}")
            continue()
        endif()
        if(_link_target STREQUAL ""
                OR IS_ABSOLUTE "${_link_target}"
                OR _link_target MATCHES "^[A-Za-z]:[/\\\\]"
                OR _link_target MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)")
            _arken_extpkg_error(
                "arken.external_packages.archive.symlink_unsafe"
                "extraction"
                "${recipe_id}"
                "Archive link target is not a bounded root-relative path: ${_link_target}"
            )
        endif()
    endforeach()
    foreach(_excluded_path IN LISTS _excluded_paths)
        list(FIND _observed_excluded_paths "${_excluded_path}"
            _observed_excluded_index)
        if(_observed_excluded_index EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.archive.exclusion_missing"
                "extraction"
                "${recipe_id}"
                "Declared excluded archive link is absent: ${_excluded_path}"
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_remove_excluded_archive_links
    root
    recipe_json
    recipe_id
)
    _arken_extpkg_archive_link_exclusions(
        _excluded_paths
        _excluded_targets
        "${recipe_json}"
        "${recipe_id}"
    )
    list(LENGTH _excluded_paths _excluded_count)
    if(_excluded_count EQUAL 0)
        return()
    endif()
    math(EXPR _excluded_last "${_excluded_count} - 1")
    foreach(_index RANGE 0 ${_excluded_last})
        list(GET _excluded_paths ${_index} _relative)
        list(GET _excluded_targets ${_index} _expected_target)
        set(_absolute "${root}/${_relative}")
        if(NOT IS_SYMLINK "${_absolute}")
            _arken_extpkg_error(
                "arken.external_packages.archive.exclusion_not_link"
                "extraction"
                "${recipe_id}"
                "Declared excluded archive link was not extracted as a link: "
                "${_relative}"
            )
        endif()
        file(READ_SYMLINK "${_absolute}" _observed_target)
        if(NOT "${_observed_target}" STREQUAL "${_expected_target}")
            _arken_extpkg_error(
                "arken.external_packages.archive.exclusion_target_mismatch"
                "extraction"
                "${recipe_id}"
                "Extracted excluded link ${_relative} expected target "
                "${_expected_target}, observed ${_observed_target}."
            )
        endif()
        file(REMOVE "${_absolute}")
    endforeach()
endfunction()

function(_arken_extpkg_validate_extracted_tree root recipe_id allow_safe_symlinks max_entries max_bytes)
    file(GLOB_RECURSE _entries LIST_DIRECTORIES TRUE RELATIVE "${root}" "${root}/*")
    list(LENGTH _entries _entry_count)
    if(_entry_count GREATER max_entries)
        _arken_extpkg_error(
            "arken.external_packages.archive.entry_limit_exceeded"
            "extraction"
            "${recipe_id}"
            "Extracted entry count ${_entry_count} exceeds limit ${max_entries}."
        )
    endif()
    set(_total_bytes 0)
    file(REAL_PATH "${root}" _root_real)
    foreach(_entry IN LISTS _entries)
        set(_absolute "${root}/${_entry}")
        if(IS_SYMLINK "${_absolute}")
            if(NOT allow_safe_symlinks)
                _arken_extpkg_error(
                    "arken.external_packages.archive.symlink_rejected"
                    "extraction"
                    "${recipe_id}"
                    "Archive contains a symlink but the recipe does not admit symlinks: ${_entry}"
                )
            endif()
            file(REAL_PATH "${_absolute}" _resolved)
            string(FIND "${_resolved}" "${_root_real}/" _prefix)
            if(NOT _prefix EQUAL 0)
                _arken_extpkg_error(
                    "arken.external_packages.archive.symlink_unsafe"
                    "extraction"
                    "${recipe_id}"
                    "Archive symlink resolves outside the extraction root: ${_entry}"
                )
            endif()
        elseif(NOT IS_DIRECTORY "${_absolute}")
            file(SIZE "${_absolute}" _size)
            math(EXPR _total_bytes "${_total_bytes} + ${_size}")
            if(_total_bytes GREATER max_bytes)
                _arken_extpkg_error(
                    "arken.external_packages.archive.expanded_size_limit_exceeded"
                    "extraction"
                    "${recipe_id}"
                    "Extracted bytes ${_total_bytes} exceed limit ${max_bytes}."
                )
            endif()
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_quarantine_cache_entry entry_path reason)
    if(NOT EXISTS "${entry_path}" AND NOT IS_SYMLINK "${entry_path}")
        return()
    endif()
    string(TIMESTAMP _timestamp "%Y%m%dT%H%M%SZ" UTC)
    string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef _nonce)
    set(_quarantine "${entry_path}.quarantine-${_timestamp}-${_nonce}")
    file(RENAME "${entry_path}" "${_quarantine}")
    message(STATUS
        "Arken external package cache quarantined '${entry_path}' as "
        "'${_quarantine}' (${reason})."
    )
endfunction()

function(_arken_extpkg_materialize_archive_source
    out_root
    out_identity
    out_receipt
    recipe_json
    recipe_path
    recipe_id
    recipe_sha
    cache_root
    offline
)
    _arken_extpkg_json_required_string(_expected_sha "${recipe_json}" "${recipe_id}" source sha256)
    _arken_extpkg_json_optional_string(
        _expected_commit
        "${recipe_json}"
        ""
        "${recipe_id}:source"
        source
        commit
    )
    string(LENGTH "${_expected_sha}" _expected_sha_length)
    if(NOT _expected_sha MATCHES "^[0-9a-f]+$"
            OR NOT _expected_sha_length EQUAL 64)
        _arken_extpkg_error(
            "arken.external_packages.archive.sha256_invalid"
            "schema"
            "${recipe_id}"
            "source.sha256 must be 64 lowercase hexadecimal characters."
        )
    endif()
    _arken_extpkg_json_array_to_list(_urls "${recipe_json}" "${recipe_id}" source urls)
    if(_urls STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.archive.urls_missing"
            "schema"
            "${recipe_id}"
            "Archive sources require at least one URL."
        )
    endif()
    _arken_extpkg_json_optional_string(_subdirectory "${recipe_json}" "" "${recipe_id}" source subdirectory)
    _arken_extpkg_json_optional_bool(_allow_safe_symlinks "${recipe_json}" FALSE "${recipe_id}" source allowSafeSymlinks)
    _arken_extpkg_json_optional_integer(_max_archive_bytes "${recipe_json}" 536870912 "${recipe_id}" source maxArchiveBytes)
    _arken_extpkg_json_optional_integer(_max_entries "${recipe_json}" 100000 "${recipe_id}" source maxEntries)
    _arken_extpkg_json_optional_integer(_max_extracted_bytes "${recipe_json}" 2147483648 "${recipe_id}" source maxExtractedBytes)

    set(_entry "${cache_root}/sources/sha256/${_expected_sha}")
    set(_receipt "${_entry}/source.receipt.json")
    set(_complete "${_entry}/.complete")
    file(MAKE_DIRECTORY "${cache_root}/locks" "${cache_root}/downloads" "${cache_root}/sources/sha256")
    # A verified cache hit can still require a complete tree-identity pass. Large
    # sources such as LLVM expand to several gigabytes and legitimately exceed
    # three minutes while another worktree holds the publication/validation lock.
    file(LOCK "${cache_root}/locks/source-${_expected_sha}.lock" GUARD FUNCTION TIMEOUT 3600)

    if(EXISTS "${_entry}")
        if(NOT EXISTS "${_receipt}" OR NOT EXISTS "${_complete}")
            _arken_extpkg_quarantine_cache_entry("${_entry}" "partial source entry")
        else()
            file(READ "${_receipt}" _receipt_json)
            string(JSON _receipt_sha ERROR_VARIABLE _receipt_error GET "${_receipt_json}" archiveSha256)
            string(JSON _tree_sha ERROR_VARIABLE _tree_error GET "${_receipt_json}" treeSha256)
            string(
                JSON
                _receipt_commit
                ERROR_VARIABLE
                _commit_error
                GET
                "${_receipt_json}"
                commit
            )
            if(NOT _receipt_error STREQUAL "NOTFOUND"
                    OR NOT _tree_error STREQUAL "NOTFOUND"
                    OR NOT "${_receipt_sha}" STREQUAL "${_expected_sha}"
                    OR (NOT _expected_commit STREQUAL ""
                        AND (NOT _commit_error STREQUAL "NOTFOUND"
                            OR NOT _receipt_commit STREQUAL _expected_commit)))
                _arken_extpkg_quarantine_cache_entry("${_entry}" "source receipt mismatch")
            else()
                set(_source_root "${_entry}/source")
                if(NOT _subdirectory STREQUAL "")
                    set(_source_root "${_source_root}/${_subdirectory}")
                endif()
                if(NOT EXISTS "${_source_root}")
                    _arken_extpkg_quarantine_cache_entry("${_entry}" "source root missing")
                else()
                    _arken_extpkg_validate_extracted_tree(
                        "${_entry}/source"
                        "${recipe_id}"
                        "${_allow_safe_symlinks}"
                        "${_max_entries}"
                        "${_max_extracted_bytes}"
                    )
                    _arken_extpkg_tree_identity(_observed_tree "${_entry}/source" "${recipe_id}")
                    if(NOT "${_observed_tree}" STREQUAL "${_tree_sha}")
                        _arken_extpkg_quarantine_cache_entry("${_entry}" "source tree hash mismatch")
                    else()
                        _arken_extpkg_verify_declared_files("${recipe_json}" "${_source_root}" "${recipe_id}")
                        _arken_extpkg_verify_expected_source_tree(
                            "${recipe_json}"
                            "${_source_root}"
                            "${recipe_id}"
                        )
                        set(_identity "${_expected_sha}:${_tree_sha}")
                        if(NOT _expected_commit STREQUAL "")
                            string(APPEND _identity ":${_expected_commit}")
                        endif()
                        set(${out_root} "${_source_root}" PARENT_SCOPE)
                        set(${out_identity} "${_identity}" PARENT_SCOPE)
                        set(${out_receipt} "${_receipt}" PARENT_SCOPE)
                        message(STATUS "Arken external package ${recipe_id}: resolved from verified cache")
                        return()
                    endif()
                endif()
            endif()
        endif()
    endif()

    if(offline)
        _arken_extpkg_error(
            "arken.external_packages.offline.source_missing"
            "acquisition"
            "${recipe_id}"
            "Offline mode is enabled and verified source ${_expected_sha} is absent. Populate the exact cache object in an authorized online build, then retry."
        )
    endif()

    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _nonce)
    set(_download "${cache_root}/downloads/${_expected_sha}.partial-${_nonce}")
    set(_downloaded FALSE)
    set(_last_error "")
    foreach(_url IN LISTS _urls)
        if(NOT _url MATCHES "^https://")
            if(NOT ARKEN_EXTERNAL_PACKAGES_ALLOW_TEST_SOURCES OR NOT _url MATCHES "^file://")
                _arken_extpkg_error(
                    "arken.external_packages.archive.url_scheme_rejected"
                    "acquisition"
                    "${recipe_id}"
                    "Archive URL must use HTTPS. Local file URLs are admitted only for controlled package-graph tests."
                )
            endif()
        endif()
        foreach(_attempt RANGE 1 2)
            file(
                DOWNLOAD
                "${_url}"
                "${_download}"
                TLS_VERIFY ON
                TIMEOUT 600
                INACTIVITY_TIMEOUT 60
                STATUS _status
            )
            list(GET _status 0 _status_code)
            if(_status_code EQUAL 0)
                file(SHA256 "${_download}" _download_sha)
                if("${_download_sha}" STREQUAL "${_expected_sha}")
                    set(_downloaded TRUE)
                    break()
                endif()
                set(_last_error
                    "attempt ${_attempt}: mirror content hash mismatch; expected ${_expected_sha}, observed ${_download_sha}"
                )
            else()
                set(_last_error "attempt ${_attempt}: ${_status}")
            endif()
            file(REMOVE "${_download}")
        endforeach()
        if(_downloaded)
            break()
        endif()
    endforeach()
    if(NOT _downloaded)
        _arken_extpkg_error(
            "arken.external_packages.archive.download_failed"
            "acquisition"
            "${recipe_id}"
            "No configured mirror delivered expected SHA-256 ${_expected_sha}. Last status: ${_last_error}"
        )
    endif()
    file(SIZE "${_download}" _archive_size)
    if(_archive_size GREATER _max_archive_bytes)
        file(REMOVE "${_download}")
        _arken_extpkg_error(
            "arken.external_packages.archive.size_limit_exceeded"
            "acquisition"
            "${recipe_id}"
            "Archive bytes ${_archive_size} exceed limit ${_max_archive_bytes}."
        )
    endif()
    _arken_extpkg_validate_archive_listing(
        "${_download}"
        "${recipe_json}"
        "${recipe_id}"
        "${_allow_safe_symlinks}"
    )

    set(_temporary "${_entry}.partial-${_nonce}")
    file(REMOVE_RECURSE "${_temporary}")
    file(MAKE_DIRECTORY "${_temporary}/source")
    file(ARCHIVE_EXTRACT INPUT "${_download}" DESTINATION "${_temporary}/source")
    file(REMOVE "${_download}")
    _arken_extpkg_remove_excluded_archive_links(
        "${_temporary}/source"
        "${recipe_json}"
        "${recipe_id}"
    )
    _arken_extpkg_validate_extracted_tree(
        "${_temporary}/source"
        "${recipe_id}"
        "${_allow_safe_symlinks}"
        "${_max_entries}"
        "${_max_extracted_bytes}"
    )
    set(_source_root "${_temporary}/source")
    if(NOT _subdirectory STREQUAL "")
        set(_source_root "${_source_root}/${_subdirectory}")
    endif()
    if(NOT EXISTS "${_source_root}")
        file(REMOVE_RECURSE "${_temporary}")
        _arken_extpkg_error(
            "arken.external_packages.archive.subdirectory_missing"
            "extraction"
            "${recipe_id}"
            "Expected source subdirectory is absent: ${_subdirectory}"
        )
    endif()
    _arken_extpkg_verify_declared_files("${recipe_json}" "${_source_root}" "${recipe_id}")
    _arken_extpkg_verify_expected_source_tree(
        "${recipe_json}"
        "${_source_root}"
        "${recipe_id}"
    )
    _arken_extpkg_tree_identity(_tree_sha "${_temporary}/source" "${recipe_id}")
    _arken_extpkg_json_escape(_expected_commit_json "${_expected_commit}")
    set(_receipt_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"sourceObjectId\": \"sha256:${_expected_sha}\",\n"
        "  \"archiveSha256\": \"${_expected_sha}\",\n"
        "  \"archiveBytes\": ${_archive_size},\n"
        "  \"treeSha256\": \"${_tree_sha}\",\n"
        "  \"commit\": \"${_expected_commit_json}\"\n"
        "}\n"
    )
    string(JOIN "" _receipt_json ${_receipt_json})
    file(WRITE "${_temporary}/source.receipt.json" "${_receipt_json}")
    file(WRITE "${_temporary}/.complete" "${_expected_sha}\n")
    file(RENAME "${_temporary}" "${_entry}" RESULT _rename_result)
    if(NOT _rename_result STREQUAL "0")
        file(REMOVE_RECURSE "${_temporary}")
        if(NOT EXISTS "${_receipt}" OR NOT EXISTS "${_complete}")
            _arken_extpkg_error(
                "arken.external_packages.cache.publish_failed"
                "cache"
                "${recipe_id}"
                "Atomic source cache publication failed: ${_rename_result}"
            )
        endif()
    endif()
    set(_source_root "${_entry}/source")
    if(NOT _subdirectory STREQUAL "")
        set(_source_root "${_source_root}/${_subdirectory}")
    endif()
    set(_identity "${_expected_sha}:${_tree_sha}")
    if(NOT _expected_commit STREQUAL "")
        string(APPEND _identity ":${_expected_commit}")
    endif()
    set(${out_root} "${_source_root}" PARENT_SCOPE)
    set(${out_identity} "${_identity}" PARENT_SCOPE)
    set(${out_receipt} "${_receipt}" PARENT_SCOPE)
    message(STATUS "Arken external package ${recipe_id}: acquired exact verified source")
endfunction()

function(_arken_extpkg_prepare_source
    out_root
    out_identity
    out_receipt
    recipe_json
    recipe_path
    recipe_id
    recipe_sha
    cache_root
    workspace_root
    offline
)
    _arken_extpkg_json_optional_string(
        _source_commit
        "${recipe_json}"
        ""
        "${recipe_id}:source"
        source
        commit
    )
    set(_override_root "")
    if(DEFINED ARKEN_EXTERNAL_PACKAGE_LOCAL_OVERRIDES
            AND NOT ARKEN_EXTERNAL_PACKAGE_LOCAL_OVERRIDES STREQUAL "")
        foreach(_override IN LISTS ARKEN_EXTERNAL_PACKAGE_LOCAL_OVERRIDES)
            string(FIND "${_override}" "=" _separator)
            if(_separator LESS 1)
                _arken_extpkg_error(
                    "arken.external_packages.override.syntax_invalid"
                    "source"
                    "${recipe_id}"
                    "Local overrides must use recipe-id=/absolute/path."
                )
            endif()
            string(SUBSTRING "${_override}" 0 ${_separator} _override_recipe)
            math(EXPR _path_start "${_separator} + 1")
            string(SUBSTRING "${_override}" ${_path_start} -1 _override_path)
            if("${_override_recipe}" STREQUAL "${recipe_id}")
                if(NOT IS_ABSOLUTE "${_override_path}")
                    _arken_extpkg_error(
                        "arken.external_packages.override.path_invalid"
                        "source"
                        "${recipe_id}"
                        "Local override path must be absolute: ${_override_path}"
                    )
                endif()
                cmake_path(NORMAL_PATH _override_path OUTPUT_VARIABLE _override_root)
            endif()
        endforeach()
    endif()
    if(NOT _override_root STREQUAL "")
        if(DEFINED ARKEN_EXTERNAL_PACKAGES_QUALIFICATION_MODE
                AND ARKEN_EXTERNAL_PACKAGES_QUALIFICATION_MODE)
            _arken_extpkg_error(
                "arken.external_packages.override.qualification_rejected"
                "source"
                "${recipe_id}"
                "Local source overrides are forbidden in qualification mode."
            )
        endif()
        _arken_extpkg_json_optional_string(
            _expected_override_tree
            "${recipe_json}"
            ""
            "${recipe_id}"
            source
            treeSha256
        )
        if(_expected_override_tree STREQUAL "")
            _arken_extpkg_error(
                "arken.external_packages.override.tree_identity_missing"
                "source"
                "${recipe_id}"
                "Recipe must declare source.treeSha256 before a local override can be identity-verified."
            )
        endif()
        _arken_extpkg_verify_declared_files("${recipe_json}" "${_override_root}" "${recipe_id}")
        _arken_extpkg_tree_identity(_override_tree "${_override_root}" "${recipe_id}")
        if(NOT "${_override_tree}" STREQUAL "${_expected_override_tree}")
            _arken_extpkg_error(
                "arken.external_packages.override.tree_hash_mismatch"
                "source"
                "${recipe_id}"
                "Override expected tree SHA-256 ${_expected_override_tree}, observed ${_override_tree}."
            )
        endif()
        set(_override_identity "local-override:${_override_tree}")
        if(NOT _source_commit STREQUAL "")
            string(APPEND _override_identity ":${_source_commit}")
        endif()
        set(${out_root} "${_override_root}" PARENT_SCOPE)
        set(${out_identity} "${_override_identity}" PARENT_SCOPE)
        set(${out_receipt} "" PARENT_SCOPE)
        message(STATUS "Arken external package ${recipe_id}: using explicit verified local override")
        return()
    endif()

    _arken_extpkg_json_required_string(_kind "${recipe_json}" "${recipe_id}" source kind)
    if(_kind STREQUAL "archive")
        _arken_extpkg_materialize_archive_source(
            _root
            _identity
            _receipt
            "${recipe_json}"
            "${recipe_path}"
            "${recipe_id}"
            "${recipe_sha}"
            "${cache_root}"
            "${offline}"
        )
    elseif(_kind STREQUAL "tracked-source" OR _kind STREQUAL "verified-prebuilt")
        _arken_extpkg_json_required_string(_root_kind "${recipe_json}" "${recipe_id}" source root)
        _arken_extpkg_json_required_string(_relative_path "${recipe_json}" "${recipe_id}" source path)
        _arken_extpkg_resolve_declared_path(
            _root
            "${_root_kind}"
            "${_relative_path}"
            "${recipe_path}"
            "${workspace_root}"
        )
        _arken_extpkg_verify_declared_files("${recipe_json}" "${_root}" "${recipe_id}")
        _arken_extpkg_tree_identity(_tree_sha "${_root}" "${recipe_id}")
        _arken_extpkg_verify_expected_source_tree("${recipe_json}" "${_root}" "${recipe_id}")
        set(_identity "${_kind}:${_tree_sha}")
        if(NOT _source_commit STREQUAL "")
            string(APPEND _identity ":${_source_commit}")
        endif()
        set(_receipt "")
        message(STATUS "Arken external package ${recipe_id}: using verified ${_kind}")
    elseif(_kind STREQUAL "system-sdk" OR _kind STREQUAL "tool-profile")
        set(_root "")
        set(_identity "${_kind}")
        set(_receipt "")
    else()
        _arken_extpkg_error(
            "arken.external_packages.source.kind_unsupported"
            "source"
            "${recipe_id}"
            "Unsupported source kind '${_kind}'."
        )
    endif()
    set(${out_root} "${_root}" PARENT_SCOPE)
    set(${out_identity} "${_identity}" PARENT_SCOPE)
    set(${out_receipt} "${_receipt}" PARENT_SCOPE)
endfunction()
