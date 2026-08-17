include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")

function(_arken_extpkg_release_workspace_ref out_var path workspace_root subject)
    if(IS_ABSOLUTE "${path}")
        file(REAL_PATH "${workspace_root}" _workspace_root_real)
        file(REAL_PATH "${path}" _path_real)
        file(RELATIVE_PATH
            _relative
            "${_workspace_root_real}"
            "${_path_real}"
        )
    else()
        set(_relative "${path}")
    endif()
    cmake_path(NORMAL_PATH _relative)
    if(_relative STREQUAL ""
            OR IS_ABSOLUTE "${_relative}"
            OR _relative MATCHES "(^|/)\\.\\.(/|$)"
            OR _relative MATCHES "\\\\")
        _arken_extpkg_error(
            "arken.external_packages.release_evidence.manifest_outside_workspace"
            "release-evidence"
            "${subject}"
            "Manifest provenance must remain a safe workspace-relative path: '${path}'."
        )
    endif()
    set(${out_var} "${_relative}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_release_increment prefix value)
    _arken_extpkg_identifier(_identifier "${value}")
    get_property(_count GLOBAL PROPERTY "${prefix}_${_identifier}")
    if(_count STREQUAL "")
        set(_count 0)
    endif()
    math(EXPR _count "${_count} + 1")
    set_property(GLOBAL PROPERTY "${prefix}_${_identifier}" "${_count}")
endfunction()

function(_arken_extpkg_release_count out_var prefix value)
    _arken_extpkg_identifier(_identifier "${value}")
    get_property(_count GLOBAL PROPERTY "${prefix}_${_identifier}")
    if(_count STREQUAL "")
        set(_count 0)
    endif()
    set(${out_var} "${_count}" PARENT_SCOPE)
endfunction()

function(arken_generate_external_package_release_evidence)
    set(options)
    set(oneValueArgs
        GRAPH_RECEIPT
        SPDX_INPUT
        INVENTORY_RECEIPT
        WORKSPACE_ROOT
        OUTPUT_PATH
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})
    foreach(_required IN ITEMS
            GRAPH_RECEIPT
            SPDX_INPUT
            WORKSPACE_ROOT
            OUTPUT_PATH)
        if(NOT ARG_${_required})
            _arken_extpkg_error(
                "arken.external_packages.release_evidence.argument_missing"
                "release-evidence"
                "external-packages"
                "${_required} is required."
            )
        endif()
    endforeach()
    foreach(_required_path IN ITEMS GRAPH_RECEIPT SPDX_INPUT WORKSPACE_ROOT)
        if(NOT EXISTS "${ARG_${_required_path}}")
            _arken_extpkg_error(
                "arken.external_packages.release_evidence.input_missing"
                "release-evidence"
                "external-packages"
                "${_required_path} is missing: '${ARG_${_required_path}}'."
            )
        endif()
    endforeach()

    file(READ "${ARG_GRAPH_RECEIPT}" _graph)
    file(READ "${ARG_SPDX_INPUT}" _spdx)
    file(SHA256 "${ARG_SPDX_INPUT}" _spdx_sha256)
    foreach(_field IN ITEMS schemaVersion platform offline)
        string(JSON "_graph_${_field}" GET "${_graph}" "${_field}")
    endforeach()
    string(JSON _spdx_format GET "${_spdx}" format)
    string(JSON _spdx_package_set_sha GET "${_spdx}" packageSetSha256)

    string(JSON _request_count LENGTH "${_graph}" requests)
    set(_request_keys "")
    if(_request_count GREATER 0)
        math(EXPR _request_last "${_request_count} - 1")
        foreach(_index RANGE 0 ${_request_last})
            string(JSON _request_id GET "${_graph}" requests ${_index} requestId)
            list(APPEND _request_keys "${_request_id}|${_index}")
        endforeach()
    endif()
    list(SORT _request_keys)
    set(_requests_json "")
    foreach(_key IN LISTS _request_keys)
        string(REPLACE "|" ";" _parts "${_key}")
        list(GET _parts 1 _index)
        foreach(_field IN ITEMS
                requestId
                recipeId
                scope
                reason
                enabledWhen
                manifest)
            string(JSON "_request_${_field}" GET
                "${_graph}" requests ${_index} "${_field}")
        endforeach()
        foreach(_field IN ITEMS conditionValue enabled)
            string(JSON "_request_${_field}" GET
                "${_graph}" requests ${_index} "${_field}")
            if(_request_${_field})
                set("_request_${_field}_json" "true")
            else()
                set("_request_${_field}_json" "false")
            endif()
        endforeach()
        _arken_extpkg_json_array_to_list(
            _request_components
            "${_graph}"
            "${_request_requestId}"
            requests ${_index} components
        )
        _arken_extpkg_json_array_to_list(
            _request_platforms
            "${_graph}"
            "${_request_requestId}"
            requests ${_index} platforms
        )
        _arken_extpkg_json_string_array(
            _request_components_json "${_request_components}"
        )
        _arken_extpkg_json_string_array(
            _request_platforms_json "${_request_platforms}"
        )
        _arken_extpkg_release_workspace_ref(
            _manifest_ref
            "${_request_manifest}"
            "${ARG_WORKSPACE_ROOT}"
            "${_request_requestId}"
        )
        _arken_extpkg_json_escape(_reason_json "${_request_reason}")
        _arken_extpkg_json_escape(
            _enabled_when_json "${_request_enabledWhen}"
        )
        _arken_extpkg_json_escape(_manifest_ref_json "${_manifest_ref}")
        if(NOT _requests_json STREQUAL "")
            string(APPEND _requests_json ",\n")
        endif()
        string(APPEND _requests_json
            "    {\"requestId\":\"${_request_requestId}\","
            "\"recipeId\":\"${_request_recipeId}\","
            "\"scope\":\"${_request_scope}\","
            "\"components\":${_request_components_json},"
            "\"platforms\":${_request_platforms_json},"
            "\"reason\":\"${_reason_json}\","
            "\"enabledWhen\":\"${_enabled_when_json}\","
            "\"conditionValue\":${_request_conditionValue_json},"
            "\"enabled\":${_request_enabled_json},"
            "\"manifestRef\":\"${_manifest_ref_json}\"}"
        )
    endforeach()
    string(SHA256 _request_set_sha256 "${_requests_json}")

    foreach(_distribution IN ITEMS
            development-only redistributable restricted system)
        _arken_extpkg_identifier(_distribution_id "${_distribution}")
        set_property(
            GLOBAL
            PROPERTY "ARKEN_EXTPKG_RELEASE_DISTRIBUTION_${_distribution_id}"
            0
        )
    endforeach()
    string(JSON _package_count LENGTH "${_graph}" packages)
    set(_package_keys "")
    if(_package_count GREATER 0)
        math(EXPR _package_last "${_package_count} - 1")
        foreach(_index RANGE 0 ${_package_last})
            string(JSON _recipe_id GET "${_graph}" packages ${_index} recipeId)
            list(APPEND _package_keys "${_recipe_id}|${_index}")
        endforeach()
    endif()
    list(SORT _package_keys)
    set(_packages_json "")
    foreach(_key IN LISTS _package_keys)
        string(REPLACE "|" ";" _parts "${_key}")
        list(GET _parts 1 _index)
        foreach(_field IN ITEMS
                recipeId
                recipeSha256
                packageId
                upstreamVersion
                adapterId
                packageInstanceId
                sourceIdentity
                license
                distribution)
            string(JSON "_package_${_field}" GET
                "${_graph}" packages ${_index} "${_field}")
        endforeach()
        _arken_extpkg_json_array_to_list(
            _package_components
            "${_graph}"
            "${_package_recipeId}"
            packages ${_index} components
        )
        _arken_extpkg_json_array_to_list(
            _package_dependencies
            "${_graph}"
            "${_package_recipeId}"
            packages ${_index} dependencies
        )
        _arken_extpkg_json_string_array(
            _package_components_json "${_package_components}"
        )
        _arken_extpkg_json_string_array(
            _package_dependencies_json "${_package_dependencies}"
        )
        _arken_extpkg_json_escape(
            _source_identity_json "${_package_sourceIdentity}"
        )
        _arken_extpkg_release_increment(
            "ARKEN_EXTPKG_RELEASE_DISTRIBUTION"
            "${_package_distribution}"
        )
        if(NOT _packages_json STREQUAL "")
            string(APPEND _packages_json ",\n")
        endif()
        string(APPEND _packages_json
            "    {\"recipeId\":\"${_package_recipeId}\","
            "\"recipeSha256\":\"${_package_recipeSha256}\","
            "\"packageId\":\"${_package_packageId}\","
            "\"upstreamVersion\":\"${_package_upstreamVersion}\","
            "\"adapterId\":\"${_package_adapterId}\","
            "\"components\":${_package_components_json},"
            "\"dependencies\":${_package_dependencies_json},"
            "\"packageInstanceId\":\"${_package_packageInstanceId}\","
            "\"sourceIdentity\":\"${_source_identity_json}\","
            "\"license\":\"${_package_license}\","
            "\"distribution\":\"${_package_distribution}\"}"
        )
    endforeach()
    string(SHA256 _package_set_sha256 "${_packages_json}")
    foreach(_distribution IN ITEMS
            development-only redistributable restricted system)
        _arken_extpkg_release_count(
            "_distribution_${_distribution}"
            "ARKEN_EXTPKG_RELEASE_DISTRIBUTION"
            "${_distribution}"
        )
    endforeach()
    if(_distribution_development-only GREATER 0
            OR _distribution_restricted GREATER 0)
        set(_distribution_ready_json "false")
        set(_qualification_status "blocked")
    else()
        set(_distribution_ready_json "true")
        set(_qualification_status "qualified")
    endif()

    if(ARG_INVENTORY_RECEIPT)
        if(NOT EXISTS "${ARG_INVENTORY_RECEIPT}")
            _arken_extpkg_error(
                "arken.external_packages.release_evidence.inventory_missing"
                "release-evidence"
                "external-packages"
                "INVENTORY_RECEIPT is missing: '${ARG_INVENTORY_RECEIPT}'."
            )
        endif()
        file(READ "${ARG_INVENTORY_RECEIPT}" _inventory)
        file(SHA256 "${ARG_INVENTORY_RECEIPT}" _inventory_receipt_sha256)
        foreach(_field IN ITEMS
                inventoryId
                inventorySchemaId
                inventorySchemaSha256
                inventorySha256
                entryCount)
            string(JSON "_inventory_${_field}" GET
                "${_inventory}" "${_field}")
        endforeach()
        foreach(_status IN ITEMS classified deferred migrated)
            string(JSON "_inventory_status_${_status}" GET
                "${_inventory}" counts status "${_status}")
        endforeach()
        string(JSON _inventory_unresolved GET
            "${_inventory}" counts releasePolicy unresolved)
        string(CONCAT _inventory_json
            "{"
            "\"inventoryId\":\"${_inventory_inventoryId}\","
            "\"inventorySchemaId\":\"${_inventory_inventorySchemaId}\","
            "\"inventorySchemaSha256\":\"${_inventory_inventorySchemaSha256}\","
            "\"inventorySha256\":\"${_inventory_inventorySha256}\","
            "\"receiptSha256\":\"${_inventory_receipt_sha256}\","
            "\"entryCount\":${_inventory_entryCount},"
            "\"statusCounts\":{"
            "\"classified\":${_inventory_status_classified},"
            "\"deferred\":${_inventory_status_deferred},"
            "\"migrated\":${_inventory_status_migrated}},"
            "\"unresolvedReleasePolicyCount\":${_inventory_unresolved}"
            "}"
        )
    else()
        set(_inventory_json "null")
        set(_inventory_receipt_sha256 "absent")
    endif()

    if(_graph_offline)
        set(_offline_json "true")
    else()
        set(_offline_json "false")
    endif()
    string(CONCAT _evidence_material
        "platform=${_graph_platform}|offline=${_offline_json}|"
        "requests=${_request_set_sha256}|packages=${_package_set_sha256}|"
        "spdx=${_spdx_sha256}|inventory=${_inventory_receipt_sha256}"
    )
    string(SHA256 _evidence_fingerprint "${_evidence_material}")
    string(CONCAT _release_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"evidenceId\": \"arken-external-package-release-evidence-v1\",\n"
        "  \"evidenceFingerprint\": \"${_evidence_fingerprint}\",\n"
        "  \"platform\": \"${_graph_platform}\",\n"
        "  \"offline\": ${_offline_json},\n"
        "  \"requestCount\": ${_request_count},\n"
        "  \"requestSetSha256\": \"${_request_set_sha256}\",\n"
        "  \"packageCount\": ${_package_count},\n"
        "  \"packageSetSha256\": \"${_package_set_sha256}\",\n"
        "  \"spdx\": {"
        "\"format\":\"${_spdx_format}\","
        "\"inputSha256\":\"${_spdx_sha256}\","
        "\"packageSetSha256\":\"${_spdx_package_set_sha}\"},\n"
        "  \"inventory\": ${_inventory_json},\n"
        "  \"qualification\": {"
        "\"status\":\"${_qualification_status}\","
        "\"distributionReady\":${_distribution_ready_json},"
        "\"distributionCounts\":{"
        "\"development-only\":${_distribution_development-only},"
        "\"redistributable\":${_distribution_redistributable},"
        "\"restricted\":${_distribution_restricted},"
        "\"system\":${_distribution_system}}},\n"
        "  \"requests\": [\n${_requests_json}\n  ],\n"
        "  \"packages\": [\n${_packages_json}\n  ]\n"
        "}\n"
    )
    foreach(_forbidden IN ITEMS
            "${ARG_WORKSPACE_ROOT}"
            "/Users/"
            "/Volumes/"
            "sessionToken"
            "bearerSecret")
        string(FIND "${_release_json}" "${_forbidden}" _forbidden_index)
        if(NOT _forbidden_index EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.release_evidence.private_material"
                "release-evidence"
                "external-packages"
                "Portable release evidence contains forbidden local/private material '${_forbidden}'."
            )
        endif()
    endforeach()
    _arken_extpkg_write_if_different("${ARG_OUTPUT_PATH}" "${_release_json}")
    set(ARKEN_EXTERNAL_PACKAGE_RELEASE_EVIDENCE
        "${ARG_OUTPUT_PATH}" PARENT_SCOPE)
endfunction()
