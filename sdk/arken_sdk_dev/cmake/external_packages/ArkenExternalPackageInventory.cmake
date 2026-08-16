include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ArkenExternalPackageJson.cmake")

function(_arken_extpkg_inventory_require_enum value subject field_name)
    set(_allowed ${ARGN})
    list(FIND _allowed "${value}" _index)
    if(_index EQUAL -1)
        string(JOIN ", " _allowed_text ${_allowed})
        _arken_extpkg_error(
            "arken.external_packages.inventory.enum_invalid"
            "inventory"
            "${subject}"
            "Field '${field_name}' has unsupported value '${value}'. Allowed values: ${_allowed_text}."
        )
    endif()
endfunction()

function(_arken_extpkg_inventory_require_sorted_unique values subject field_name)
    set(_values ${values})
    list(LENGTH _values _before)
    set(_unique "${_values}")
    list(REMOVE_DUPLICATES _unique)
    list(LENGTH _unique _after)
    if(NOT _before EQUAL _after)
        _arken_extpkg_error(
            "arken.external_packages.inventory.duplicate_value"
            "inventory"
            "${subject}"
            "Field '${field_name}' contains duplicate values."
        )
    endif()
    set(_sorted "${_values}")
    list(SORT _sorted)
    if(NOT "${_values}" STREQUAL "${_sorted}")
        _arken_extpkg_error(
            "arken.external_packages.inventory.order_invalid"
            "inventory"
            "${subject}"
            "Field '${field_name}' must be lexically sorted for deterministic review and receipt generation."
        )
    endif()
endfunction()

function(_arken_extpkg_inventory_require_repo_path path workspace_root subject field_name)
    if(path STREQUAL ""
            OR IS_ABSOLUTE "${path}"
            OR path MATCHES "(^|/)\\.\\.(/|$)"
            OR path MATCHES "\\\\")
        _arken_extpkg_error(
            "arken.external_packages.inventory.path_unsafe"
            "inventory"
            "${subject}"
            "Field '${field_name}' must be a safe repository-relative path: '${path}'."
        )
    endif()
    cmake_path(
        ABSOLUTE_PATH path
        BASE_DIRECTORY "${workspace_root}"
        NORMALIZE
        OUTPUT_VARIABLE _absolute
    )
    set(_workspace_root "${workspace_root}")
    cmake_path(NORMAL_PATH _workspace_root)
    cmake_path(IS_PREFIX _workspace_root "${_absolute}" NORMALIZE _inside_workspace)
    if(NOT _inside_workspace)
        _arken_extpkg_error(
            "arken.external_packages.inventory.path_outside_workspace"
            "inventory"
            "${subject}"
            "Field '${field_name}' resolves outside the workspace: '${path}'."
        )
    endif()
    if(NOT EXISTS "${_absolute}")
        _arken_extpkg_error(
            "arken.external_packages.inventory.path_missing"
            "inventory"
            "${subject}"
            "Field '${field_name}' identifies a missing repository path: '${path}'."
        )
    endif()
endfunction()

function(_arken_extpkg_inventory_recipe_class_for_disposition out_var disposition subject)
    if(disposition STREQUAL "source-package")
        set(_class "source-package")
    elseif(disposition STREQUAL "tracked-source")
        set(_class "tracked-source")
    elseif(disposition STREQUAL "verified-prebuilt")
        set(_class "verified-prebuilt")
    elseif(disposition STREQUAL "tool-profile")
        set(_class "build-tool")
    else()
        _arken_extpkg_error(
            "arken.external_packages.inventory.recipe_disposition_invalid"
            "inventory"
            "${subject}"
            "Recipe authority cannot represent disposition '${disposition}'."
        )
    endif()
    set(${out_var} "${_class}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_inventory_increment prefix value)
    _arken_extpkg_identifier(_identifier "${value}")
    get_property(_count GLOBAL PROPERTY "${prefix}_${_identifier}")
    if(_count STREQUAL "")
        set(_count 0)
    endif()
    math(EXPR _count "${_count} + 1")
    set_property(GLOBAL PROPERTY "${prefix}_${_identifier}" "${_count}")
endfunction()

function(_arken_extpkg_inventory_count out_var prefix value)
    _arken_extpkg_identifier(_identifier "${value}")
    get_property(_count GLOBAL PROPERTY "${prefix}_${_identifier}")
    if(_count STREQUAL "")
        set(_count 0)
    endif()
    set(${out_var} "${_count}" PARENT_SCOPE)
endfunction()

function(arken_validate_external_package_inventory)
    set(options)
    set(oneValueArgs
        INVENTORY_PATH
        RECIPE_ROOT
        WORKSPACE_ROOT
        OUTPUT_RECEIPT
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})
    foreach(_required IN ITEMS
            INVENTORY_PATH
            RECIPE_ROOT
            WORKSPACE_ROOT
            OUTPUT_RECEIPT)
        if(NOT ARG_${_required})
            _arken_extpkg_error(
                "arken.external_packages.inventory.argument_missing"
                "inventory"
                "external-dependency-inventory"
                "${_required} is required."
            )
        endif()
    endforeach()
    foreach(_path_field IN ITEMS INVENTORY_PATH RECIPE_ROOT WORKSPACE_ROOT)
        if(NOT EXISTS "${ARG_${_path_field}}")
            _arken_extpkg_error(
                "arken.external_packages.inventory.input_missing"
                "inventory"
                "external-dependency-inventory"
                "${_path_field} does not exist: '${ARG_${_path_field}}'."
            )
        endif()
    endforeach()

    file(READ "${ARG_INVENTORY_PATH}" _inventory_json)
    file(SHA256 "${ARG_INVENTORY_PATH}" _inventory_sha256)
    get_filename_component(_inventory_directory "${ARG_INVENTORY_PATH}" DIRECTORY)
    set(_schema_path
        "${_inventory_directory}/dependency-inventory.schema.json")
    if(NOT EXISTS "${_schema_path}")
        _arken_extpkg_error(
            "arken.external_packages.inventory.schema_missing"
            "inventory"
            "external-dependency-inventory"
            "The inventory schema is missing: '${_schema_path}'."
        )
    endif()
    file(READ "${_schema_path}" _schema_json)
    file(SHA256 "${_schema_path}" _schema_sha256)
    _arken_extpkg_json_required_string(
        _schema_id
        "${_schema_json}"
        "external-dependency-inventory-schema"
        "$id"
    )
    if(NOT _schema_id STREQUAL
            "https://arken.gg/schemas/external-dependency-inventory-v1.json")
        _arken_extpkg_error(
            "arken.external_packages.inventory.schema_id_invalid"
            "inventory"
            "external-dependency-inventory"
            "Unexpected inventory schema identity '${_schema_id}'."
        )
    endif()
    _arken_extpkg_validate_object_keys(
        "${_inventory_json}"
        "external-dependency-inventory"
        "schemaVersion;inventoryId;entries"
    )
    string(
        JSON _schema_version
        ERROR_VARIABLE _schema_error
        GET "${_inventory_json}" schemaVersion
    )
    string(
        JSON _schema_type
        ERROR_VARIABLE _schema_type_error
        TYPE "${_inventory_json}" schemaVersion
    )
    if(NOT _schema_error STREQUAL "NOTFOUND"
            OR NOT _schema_type_error STREQUAL "NOTFOUND"
            OR NOT _schema_type STREQUAL "NUMBER"
            OR NOT _schema_version STREQUAL "1")
        _arken_extpkg_error(
            "arken.external_packages.inventory.schema_version_invalid"
            "inventory"
            "external-dependency-inventory"
            "schemaVersion must be the integer 1."
        )
    endif()
    _arken_extpkg_json_required_string(
        _inventory_id
        "${_inventory_json}"
        "external-dependency-inventory"
        inventoryId
    )
    if(NOT _inventory_id MATCHES "^[a-z0-9][a-z0-9._-]*-v[0-9]+$")
        _arken_extpkg_error(
            "arken.external_packages.inventory.id_invalid"
            "inventory"
            "external-dependency-inventory"
            "inventoryId '${_inventory_id}' must be a stable lowercase versioned identifier."
        )
    endif()
    string(
        JSON _entry_count
        ERROR_VARIABLE _entries_error
        LENGTH "${_inventory_json}" entries
    )
    string(
        JSON _entries_type
        ERROR_VARIABLE _entries_type_error
        TYPE "${_inventory_json}" entries
    )
    if(NOT _entries_error STREQUAL "NOTFOUND"
            OR NOT _entries_type_error STREQUAL "NOTFOUND"
            OR NOT _entries_type STREQUAL "ARRAY"
            OR _entry_count LESS 1)
        _arken_extpkg_error(
            "arken.external_packages.inventory.entries_invalid"
            "inventory"
            "${_inventory_id}"
            "entries must be a non-empty array."
        )
    endif()

    foreach(_counter_prefix IN ITEMS
            ARKEN_EXTPKG_INVENTORY_DISPOSITION
            ARKEN_EXTPKG_INVENTORY_STATUS
            ARKEN_EXTPKG_INVENTORY_RELEASE
            ARKEN_EXTPKG_INVENTORY_AUTHORITY)
        foreach(_value IN ITEMS
                source-package
                tracked-source
                verified-prebuilt
                system-sdk
                tool-profile
                deferred
                migrated
                classified
                redistributable
                system
                development-only
                restricted
                unresolved
                recipe
                tracked-path
                cmake-package
                platform-sdk
                project-resolver)
            _arken_extpkg_identifier(_counter_identifier "${_value}")
            set_property(
                GLOBAL
                PROPERTY "${_counter_prefix}_${_counter_identifier}"
                0
            )
        endforeach()
    endforeach()

    set(_dependency_ids "")
    set(_entries_json "")
    math(EXPR _last_entry "${_entry_count} - 1")
    foreach(_index RANGE 0 ${_last_entry})
        string(JSON _entry GET "${_inventory_json}" entries ${_index})
        set(_entry_subject "${_inventory_id}.entries[${_index}]")
        _arken_extpkg_validate_object_keys(
            "${_entry}"
            "${_entry_subject}"
            "dependencyId;displayName;roles;disposition;status;authority;owner;platforms;releasePolicy;evidence;reason;prerequisite"
        )
        foreach(_field IN ITEMS
                dependencyId
                displayName
                disposition
                status
                owner
                releasePolicy
                reason)
            _arken_extpkg_json_required_string(
                "_${_field}"
                "${_entry}"
                "${_entry_subject}"
                "${_field}"
            )
        endforeach()
        if(NOT _dependencyId MATCHES "^[a-z0-9][a-z0-9._-]*$")
            _arken_extpkg_error(
                "arken.external_packages.inventory.dependency_id_invalid"
                "inventory"
                "${_entry_subject}"
                "dependencyId '${_dependencyId}' is invalid."
            )
        endif()
        list(APPEND _dependency_ids "${_dependencyId}")
        set(_entry_subject "${_inventory_id}:${_dependencyId}")

        _arken_extpkg_json_array_to_list(
            _roles "${_entry}" "${_entry_subject}" roles
        )
        _arken_extpkg_json_array_to_list(
            _platforms "${_entry}" "${_entry_subject}" platforms
        )
        _arken_extpkg_json_array_to_list(
            _evidence "${_entry}" "${_entry_subject}" evidence
        )
        foreach(_field IN ITEMS roles platforms evidence)
            if(NOT _${_field})
                _arken_extpkg_error(
                    "arken.external_packages.inventory.array_empty"
                    "inventory"
                    "${_entry_subject}"
                    "Field '${_field}' must contain at least one value."
                )
            endif()
            _arken_extpkg_inventory_require_sorted_unique(
                "${_${_field}}" "${_entry_subject}" "${_field}"
            )
        endforeach()
        foreach(_role IN LISTS _roles)
            _arken_extpkg_inventory_require_enum(
                "${_role}" "${_entry_subject}" "roles"
                build-tool
                code-generator
                header-library
                package-backend
                runtime-library
                system-framework
                test-library
                toolchain
            )
        endforeach()
        foreach(_platform IN LISTS _platforms)
            _arken_extpkg_inventory_require_enum(
                "${_platform}" "${_entry_subject}" "platforms"
                android any ios linux macos web windows
            )
        endforeach()
        list(FIND _platforms "any" _any_platform_index)
        list(LENGTH _platforms _platform_count)
        if(NOT _any_platform_index EQUAL -1 AND _platform_count GREATER 1)
            _arken_extpkg_error(
                "arken.external_packages.inventory.platform_any_mixed"
                "inventory"
                "${_entry_subject}"
                "Platform 'any' cannot be combined with named platforms."
            )
        endif()
        _arken_extpkg_inventory_require_enum(
            "${_disposition}" "${_entry_subject}" "disposition"
            deferred
            source-package
            system-sdk
            tool-profile
            tracked-source
            verified-prebuilt
        )
        _arken_extpkg_inventory_require_enum(
            "${_status}" "${_entry_subject}" "status"
            classified deferred migrated
        )
        _arken_extpkg_inventory_require_enum(
            "${_releasePolicy}" "${_entry_subject}" "releasePolicy"
            development-only redistributable restricted system unresolved
        )

        _arken_extpkg_validate_object_keys(
            "${_entry}"
            "${_entry_subject}.authority"
            "kind;ref"
            authority
        )
        _arken_extpkg_json_required_string(
            _authority_kind
            "${_entry}"
            "${_entry_subject}"
            authority kind
        )
        _arken_extpkg_json_required_string(
            _authority_ref
            "${_entry}"
            "${_entry_subject}"
            authority ref
        )
        _arken_extpkg_inventory_require_enum(
            "${_authority_kind}" "${_entry_subject}" "authority.kind"
            cmake-package
            deferred
            platform-sdk
            project-resolver
            recipe
            tracked-path
        )
        _arken_extpkg_json_optional_string(
            _prerequisite
            "${_entry}"
            ""
            "${_entry_subject}"
            prerequisite
        )

        _arken_extpkg_inventory_require_repo_path(
            "${_owner}" "${ARG_WORKSPACE_ROOT}" "${_entry_subject}" "owner"
        )
        foreach(_evidence_path IN LISTS _evidence)
            _arken_extpkg_inventory_require_repo_path(
                "${_evidence_path}"
                "${ARG_WORKSPACE_ROOT}"
                "${_entry_subject}"
                "evidence"
            )
        endforeach()

        set(_recipe_sha256 "")
        if(_status STREQUAL "deferred")
            if(NOT _disposition STREQUAL "deferred"
                    OR NOT _authority_kind STREQUAL "deferred"
                    OR NOT _releasePolicy STREQUAL "unresolved"
                    OR _prerequisite STREQUAL "")
                _arken_extpkg_error(
                    "arken.external_packages.inventory.deferred_contract_invalid"
                    "inventory"
                    "${_entry_subject}"
                    "Deferred entries require deferred disposition/authority, unresolved release policy, and a non-empty prerequisite."
                )
            endif()
        elseif(_disposition STREQUAL "deferred"
                OR _authority_kind STREQUAL "deferred")
            _arken_extpkg_error(
                "arken.external_packages.inventory.non_deferred_contract_invalid"
                "inventory"
                "${_entry_subject}"
                "Only status 'deferred' may use deferred disposition or authority."
            )
        elseif(_status STREQUAL "migrated")
            if(NOT _authority_kind STREQUAL "recipe")
                _arken_extpkg_error(
                    "arken.external_packages.inventory.migrated_authority_invalid"
                    "inventory"
                    "${_entry_subject}"
                    "Migrated dependencies require recipe authority."
                )
            endif()
            set(_recipe_path "${ARG_RECIPE_ROOT}/${_authority_ref}.json")
            if(NOT EXISTS "${_recipe_path}")
                _arken_extpkg_error(
                    "arken.external_packages.inventory.recipe_missing"
                    "inventory"
                    "${_entry_subject}"
                    "Recipe authority '${_authority_ref}' is missing at '${_recipe_path}'."
                )
            endif()
            file(READ "${_recipe_path}" _recipe_json)
            file(SHA256 "${_recipe_path}" _recipe_sha256)
            _arken_extpkg_json_required_string(
                _recipe_id
                "${_recipe_json}"
                "${_entry_subject}"
                recipeId
            )
            _arken_extpkg_json_required_string(
                _recipe_class
                "${_recipe_json}"
                "${_entry_subject}"
                dependencyClass
            )
            _arken_extpkg_json_required_string(
                _recipe_distribution
                "${_recipe_json}"
                "${_entry_subject}"
                license distribution
            )
            if(NOT _recipe_id STREQUAL _authority_ref)
                _arken_extpkg_error(
                    "arken.external_packages.inventory.recipe_id_mismatch"
                    "inventory"
                    "${_entry_subject}"
                    "Recipe file reports '${_recipe_id}' but inventory authority is '${_authority_ref}'."
                )
            endif()
            _arken_extpkg_inventory_recipe_class_for_disposition(
                _expected_recipe_class "${_disposition}" "${_entry_subject}"
            )
            if(NOT _recipe_class STREQUAL _expected_recipe_class)
                _arken_extpkg_error(
                    "arken.external_packages.inventory.recipe_class_mismatch"
                    "inventory"
                    "${_entry_subject}"
                    "Disposition '${_disposition}' requires recipe class '${_expected_recipe_class}', observed '${_recipe_class}'."
                )
            endif()
            if(NOT _recipe_distribution STREQUAL _releasePolicy)
                _arken_extpkg_error(
                    "arken.external_packages.inventory.release_policy_mismatch"
                    "inventory"
                    "${_entry_subject}"
                    "Inventory release policy '${_releasePolicy}' does not match recipe distribution '${_recipe_distribution}'."
                )
            endif()
        elseif(_authority_kind STREQUAL "recipe")
            _arken_extpkg_error(
                "arken.external_packages.inventory.classified_recipe_invalid"
                "inventory"
                "${_entry_subject}"
                "Recipe authority is reserved for migrated entries."
            )
        endif()

        if(_authority_kind STREQUAL "tracked-path"
                OR _authority_kind STREQUAL "project-resolver")
            _arken_extpkg_inventory_require_repo_path(
                "${_authority_ref}"
                "${ARG_WORKSPACE_ROOT}"
                "${_entry_subject}"
                "authority.ref"
            )
        endif()

        _arken_extpkg_inventory_increment(
            "ARKEN_EXTPKG_INVENTORY_DISPOSITION" "${_disposition}"
        )
        _arken_extpkg_inventory_increment(
            "ARKEN_EXTPKG_INVENTORY_STATUS" "${_status}"
        )
        _arken_extpkg_inventory_increment(
            "ARKEN_EXTPKG_INVENTORY_RELEASE" "${_releasePolicy}"
        )
        _arken_extpkg_inventory_increment(
            "ARKEN_EXTPKG_INVENTORY_AUTHORITY" "${_authority_kind}"
        )

        _arken_extpkg_json_escape(_display_name_json "${_displayName}")
        _arken_extpkg_json_escape(_authority_ref_json "${_authority_ref}")
        _arken_extpkg_json_escape(_owner_json "${_owner}")
        _arken_extpkg_json_escape(_reason_json "${_reason}")
        _arken_extpkg_json_escape(_prerequisite_json "${_prerequisite}")
        _arken_extpkg_json_string_array(_roles_json "${_roles}")
        _arken_extpkg_json_string_array(_platforms_json "${_platforms}")
        _arken_extpkg_json_string_array(_evidence_json "${_evidence}")
        if(NOT _entries_json STREQUAL "")
            string(APPEND _entries_json ",\n")
        endif()
        string(APPEND _entries_json
            "    {\"dependencyId\":\"${_dependencyId}\","
            "\"displayName\":\"${_display_name_json}\","
            "\"roles\":${_roles_json},"
            "\"disposition\":\"${_disposition}\","
            "\"status\":\"${_status}\","
            "\"authority\":{\"kind\":\"${_authority_kind}\",\"ref\":\"${_authority_ref_json}\"},"
            "\"authoritySha256\":\"${_recipe_sha256}\","
            "\"owner\":\"${_owner_json}\","
            "\"platforms\":${_platforms_json},"
            "\"releasePolicy\":\"${_releasePolicy}\","
            "\"evidence\":${_evidence_json},"
            "\"reason\":\"${_reason_json}\","
            "\"prerequisite\":\"${_prerequisite_json}\"}"
        )
    endforeach()

    _arken_extpkg_inventory_require_sorted_unique(
        "${_dependency_ids}" "${_inventory_id}" "entries[].dependencyId"
    )

    foreach(_value IN ITEMS
            deferred
            source-package
            system-sdk
            tool-profile
            tracked-source
            verified-prebuilt)
        _arken_extpkg_inventory_count(
            "_disposition_${_value}"
            "ARKEN_EXTPKG_INVENTORY_DISPOSITION"
            "${_value}"
        )
    endforeach()
    foreach(_value IN ITEMS classified deferred migrated)
        _arken_extpkg_inventory_count(
            "_status_${_value}"
            "ARKEN_EXTPKG_INVENTORY_STATUS"
            "${_value}"
        )
    endforeach()
    foreach(_value IN ITEMS
            development-only redistributable restricted system unresolved)
        _arken_extpkg_inventory_count(
            "_release_${_value}"
            "ARKEN_EXTPKG_INVENTORY_RELEASE"
            "${_value}"
        )
    endforeach()
    foreach(_value IN ITEMS
            cmake-package
            deferred
            platform-sdk
            project-resolver
            recipe
            tracked-path)
        _arken_extpkg_inventory_count(
            "_authority_${_value}"
            "ARKEN_EXTPKG_INVENTORY_AUTHORITY"
            "${_value}"
        )
    endforeach()

    string(CONCAT _receipt_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"inventorySchemaId\": \"${_schema_id}\",\n"
        "  \"inventorySchemaSha256\": \"${_schema_sha256}\",\n"
        "  \"inventoryId\": \"${_inventory_id}\",\n"
        "  \"inventorySha256\": \"${_inventory_sha256}\",\n"
        "  \"entryCount\": ${_entry_count},\n"
        "  \"counts\": {\n"
        "    \"disposition\": {"
        "\"deferred\":${_disposition_deferred},"
        "\"source-package\":${_disposition_source-package},"
        "\"system-sdk\":${_disposition_system-sdk},"
        "\"tool-profile\":${_disposition_tool-profile},"
        "\"tracked-source\":${_disposition_tracked-source},"
        "\"verified-prebuilt\":${_disposition_verified-prebuilt}},\n"
        "    \"status\": {"
        "\"classified\":${_status_classified},"
        "\"deferred\":${_status_deferred},"
        "\"migrated\":${_status_migrated}},\n"
        "    \"releasePolicy\": {"
        "\"development-only\":${_release_development-only},"
        "\"redistributable\":${_release_redistributable},"
        "\"restricted\":${_release_restricted},"
        "\"system\":${_release_system},"
        "\"unresolved\":${_release_unresolved}},\n"
        "    \"authority\": {"
        "\"cmake-package\":${_authority_cmake-package},"
        "\"deferred\":${_authority_deferred},"
        "\"platform-sdk\":${_authority_platform-sdk},"
        "\"project-resolver\":${_authority_project-resolver},"
        "\"recipe\":${_authority_recipe},"
        "\"tracked-path\":${_authority_tracked-path}}\n"
        "  },\n"
        "  \"entries\": [\n${_entries_json}\n  ]\n"
        "}\n"
    )
    _arken_extpkg_write_if_different("${ARG_OUTPUT_RECEIPT}" "${_receipt_json}")
    _arken_extpkg_add_configure_dependency("${ARG_INVENTORY_PATH}")
    _arken_extpkg_add_configure_dependency("${_schema_path}")
    set(ARKEN_EXTERNAL_PACKAGE_INVENTORY_RECEIPT
        "${ARG_OUTPUT_RECEIPT}" PARENT_SCOPE)
endfunction()
