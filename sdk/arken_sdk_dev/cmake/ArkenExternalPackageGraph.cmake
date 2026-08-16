include_guard(GLOBAL)

set(_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/external_packages")
include("${_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR}/ArkenExternalPackageJson.cmake")
include("${_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR}/ArkenExternalPackageSource.cmake")
include("${_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR}/ArkenExternalPackagePatch.cmake")
include("${_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR}/ArkenExternalPackageAdapters.cmake")

function(_arken_extpkg_default_cache_root out_var)
    if(DEFINED ARKEN_EXTERNAL_PACKAGE_CACHE_ROOT
            AND NOT ARKEN_EXTERNAL_PACKAGE_CACHE_ROOT STREQUAL "")
        cmake_path(
            ABSOLUTE_PATH
            ARKEN_EXTERNAL_PACKAGE_CACHE_ROOT
            NORMALIZE
            OUTPUT_VARIABLE _cache_root
        )
    elseif(APPLE AND DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(_cache_root "$ENV{HOME}/Library/Caches/Arken/external-packages/v1")
    elseif(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(_cache_root "$ENV{XDG_CACHE_HOME}/arken/external-packages/v1")
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(_cache_root "$ENV{HOME}/.cache/arken/external-packages/v1")
    else()
        set(_cache_root "${CMAKE_BINARY_DIR}/_arken/external-package-cache-v1")
    endif()
    set(${out_var} "${_cache_root}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_reset_graph_state)
    foreach(_property IN ITEMS
            ARKEN_EXTPKG_REQUEST_IDS
            ARKEN_EXTPKG_RECIPE_IDS
            ARKEN_EXTPKG_RESOLVING
            ARKEN_EXTPKG_RESOLVED
            ARKEN_EXTPKG_PACKAGE_IDS
            ARKEN_EXTPKG_TARGET_NAMES)
        set_property(GLOBAL PROPERTY "${_property}" "")
    endforeach()
endfunction()

function(_arken_extpkg_validate_consumer_record record manifest_path index current_platform)
    set(_subject "${manifest_path}:external_dependencies[${index}]")
    _arken_extpkg_validate_object_keys(
        "${record}"
        "${_subject}"
        "recipe;scope;components;platforms;reason"
    )
    _arken_extpkg_json_required_string(_recipe "${record}" "${_subject}" recipe)
    _arken_extpkg_json_required_string(_scope "${record}" "${_subject}" scope)
    if(NOT _recipe MATCHES "^[a-z0-9][a-z0-9._-]*-v[0-9]+$")
        _arken_extpkg_error(
            "arken.external_packages.consumer.recipe_id_invalid"
            "manifest"
            "${_subject}"
            "Recipe '${_recipe}' must be an immutable lowercase ID ending in -v<number>."
        )
    endif()
    if(NOT _scope STREQUAL "public"
            AND NOT _scope STREQUAL "private"
            AND NOT _scope STREQUAL "test")
        _arken_extpkg_error(
            "arken.external_packages.consumer.scope_invalid"
            "manifest"
            "${_subject}"
            "Scope '${_scope}' is invalid. Expected public, private, or test."
        )
    endif()
    _arken_extpkg_json_array_to_list(_components "${record}" "${_subject}" components)
    _arken_extpkg_require_unique_list("${_components}" "${_subject}" components)
    list(REMOVE_DUPLICATES _components)
    list(SORT _components)
    _arken_extpkg_json_array_to_list(_platforms "${record}" "${_subject}" platforms)
    _arken_extpkg_require_unique_list("${_platforms}" "${_subject}" platforms)
    foreach(_platform IN LISTS _platforms)
        if(NOT _platform STREQUAL "macos"
                AND NOT _platform STREQUAL "linux"
                AND NOT _platform STREQUAL "windows"
                AND NOT _platform STREQUAL "android"
                AND NOT _platform STREQUAL "ios"
                AND NOT _platform STREQUAL "web")
            _arken_extpkg_error(
                "arken.external_packages.consumer.platform_invalid"
                "manifest"
                "${_subject}"
                "Platform '${_platform}' is invalid."
            )
        endif()
    endforeach()
    set(_enabled TRUE)
    if(_scope STREQUAL "test" AND NOT BUILD_TESTING)
        set(_enabled FALSE)
    endif()
    if(NOT _platforms STREQUAL "")
        list(FIND _platforms "${current_platform}" _platform_index)
        if(_platform_index EQUAL -1)
            set(_enabled FALSE)
        endif()
    endif()
    _arken_extpkg_json_optional_string(_reason "${record}" "" "${_subject}" reason)
    string(SHA256 _request_id
        "${manifest_path}|${index}|${_recipe}|${_scope}|${_components}|${_platforms}"
    )
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_RECIPE_${_request_id}" "${_recipe}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_SCOPE_${_request_id}" "${_scope}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_COMPONENTS_${_request_id}" "${_components}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_PLATFORMS_${_request_id}" "${_platforms}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_REASON_${_request_id}" "${_reason}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_MANIFEST_${_request_id}" "${manifest_path}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_ENABLED_${_request_id}" "${_enabled}")
    set_property(GLOBAL APPEND PROPERTY ARKEN_EXTPKG_REQUEST_IDS "${_request_id}")
    if(_enabled)
        _arken_extpkg_identifier(_recipe_ident "${_recipe}")
        get_property(_recipe_components GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_recipe_ident}")
        list(APPEND _recipe_components ${_components})
        list(REMOVE_DUPLICATES _recipe_components)
        list(SORT _recipe_components)
        set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_recipe_ident}" "${_recipe_components}")
        set_property(GLOBAL APPEND PROPERTY ARKEN_EXTPKG_RECIPE_IDS "${_recipe}")
    endif()
endfunction()

function(_arken_extpkg_collect_manifest manifest_path current_platform)
    if(NOT EXISTS "${manifest_path}")
        _arken_extpkg_error(
            "arken.external_packages.manifest.missing"
            "manifest"
            "${manifest_path}"
            "Enabled module manifest does not exist."
        )
    endif()
    _arken_extpkg_add_configure_dependency("${manifest_path}")
    file(READ "${manifest_path}" _manifest_json)
    _arken_extpkg_json_type(_type "${_manifest_json}" external_dependencies)
    if(_type STREQUAL "")
        return()
    endif()
    if(NOT _type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.consumer.array_invalid"
            "manifest"
            "${manifest_path}"
            "external_dependencies must be an array."
        )
    endif()
    string(JSON _length LENGTH "${_manifest_json}" external_dependencies)
    if(_length EQUAL 0)
        return()
    endif()
    math(EXPR _last "${_length} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _record_type TYPE "${_manifest_json}" external_dependencies ${_index})
        if(NOT _record_type STREQUAL "OBJECT")
            _arken_extpkg_error(
                "arken.external_packages.consumer.record_invalid"
                "manifest"
                "${manifest_path}"
                "external_dependencies[${_index}] must be an object."
            )
        endif()
        string(JSON _record GET "${_manifest_json}" external_dependencies ${_index})
        _arken_extpkg_validate_consumer_record(
            "${_record}"
            "${manifest_path}"
            "${_index}"
            "${current_platform}"
        )
    endforeach()
endfunction()

function(_arken_extpkg_validate_recipe recipe_id recipe_path recipe_json current_platform)
    _arken_extpkg_validate_object_keys(
        "${recipe_json}"
        "${recipe_id}"
        "schemaVersion;recipeId;packageId;upstreamVersion;dependencyClass;supportedPlatforms;source;patches;adapter;license;components;dependencies;testOnly"
    )
    string(JSON _schema ERROR_VARIABLE _schema_error GET "${recipe_json}" schemaVersion)
    if(NOT _schema_error STREQUAL "NOTFOUND" OR NOT _schema EQUAL 1)
        _arken_extpkg_error(
            "arken.external_packages.recipe.schema_version_unsupported"
            "schema"
            "${recipe_id}"
            "schemaVersion must be exactly 1."
        )
    endif()
    _arken_extpkg_json_required_string(_declared_id "${recipe_json}" "${recipe_id}" recipeId)
    if(NOT "${_declared_id}" STREQUAL "${recipe_id}")
        _arken_extpkg_error(
            "arken.external_packages.recipe.identity_mismatch"
            "schema"
            "${recipe_id}"
            "Recipe file declares '${_declared_id}', expected '${recipe_id}'."
        )
    endif()
    _arken_extpkg_json_required_string(_package_id "${recipe_json}" "${recipe_id}" packageId)
    _arken_extpkg_json_required_string(_upstream_version "${recipe_json}" "${recipe_id}" upstreamVersion)
    _arken_extpkg_json_required_string(_dependency_class "${recipe_json}" "${recipe_id}" dependencyClass)
    if(NOT _package_id MATCHES "^[a-z0-9][a-z0-9._-]*$")
        _arken_extpkg_error(
            "arken.external_packages.recipe.package_id_invalid"
            "schema"
            "${recipe_id}"
            "packageId '${_package_id}' must be lowercase and stable."
        )
    endif()
    if(NOT _dependency_class STREQUAL "source-package"
            AND NOT _dependency_class STREQUAL "tracked-source"
            AND NOT _dependency_class STREQUAL "verified-prebuilt"
            AND NOT _dependency_class STREQUAL "system-sdk"
            AND NOT _dependency_class STREQUAL "build-tool")
        _arken_extpkg_error(
            "arken.external_packages.recipe.dependency_class_invalid"
            "schema"
            "${recipe_id}"
            "Unsupported dependencyClass '${_dependency_class}'."
        )
    endif()
    _arken_extpkg_json_array_to_list(_supported_platforms "${recipe_json}" "${recipe_id}" supportedPlatforms)
    _arken_extpkg_require_unique_list(
        "${_supported_platforms}"
        "${recipe_id}"
        supportedPlatforms
    )
    if(_supported_platforms STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.recipe.platforms_missing"
            "schema"
            "${recipe_id}"
            "supportedPlatforms must name at least one platform or 'any'."
        )
    endif()
    list(FIND _supported_platforms "any" _any_index)
    list(FIND _supported_platforms "${current_platform}" _platform_index)
    if(_any_index EQUAL -1 AND _platform_index EQUAL -1)
        _arken_extpkg_error(
            "arken.external_packages.recipe.platform_unsupported"
            "resolution"
            "${recipe_id}"
            "Recipe does not support target platform '${current_platform}'."
        )
    endif()
    _arken_extpkg_validate_object_keys(
        "${recipe_json}"
        "${recipe_id}:source"
        "kind;root;path;urls;sha256;treeSha256;subdirectory;files;maxArchiveBytes;maxEntries;maxExtractedBytes;allowSafeSymlinks"
        source
    )
    _arken_extpkg_validate_object_keys(
        "${recipe_json}"
        "${recipe_id}:adapter"
        "kind;version;options;configureArguments;cmakeSubdirectory;findPackage;makeToolTarget;patchToolTarget"
        adapter
    )
    _arken_extpkg_validate_object_keys(
        "${recipe_json}"
        "${recipe_id}:license"
        "spdx;distribution;files"
        license
    )
    _arken_extpkg_json_required_string(_adapter_kind "${recipe_json}" "${recipe_id}" adapter kind)
    _arken_extpkg_json_optional_integer(_adapter_version "${recipe_json}" 1 "${recipe_id}" adapter version)
    if(_adapter_version LESS 1)
        _arken_extpkg_error(
            "arken.external_packages.adapter.version_invalid"
            "schema"
            "${recipe_id}"
            "Adapter version must be positive."
        )
    endif()
    if(NOT _adapter_kind STREQUAL "header-only"
            AND NOT _adapter_kind STREQUAL "selected-sources"
            AND NOT _adapter_kind STREQUAL "tracked-source"
            AND NOT _adapter_kind STREQUAL "cmake-source"
            AND NOT _adapter_kind STREQUAL "configure-make"
            AND NOT _adapter_kind STREQUAL "verified-prebuilt"
            AND NOT _adapter_kind STREQUAL "system-sdk"
            AND NOT _adapter_kind STREQUAL "tool-profile")
        _arken_extpkg_error(
            "arken.external_packages.adapter.unsupported"
            "schema"
            "${recipe_id}"
            "Adapter '${_adapter_kind}' is not in the reviewed v1 adapter set."
        )
    endif()
    _arken_extpkg_json_required_string(_license_spdx "${recipe_json}" "${recipe_id}" license spdx)
    _arken_extpkg_json_required_string(_distribution "${recipe_json}" "${recipe_id}" license distribution)
    if(NOT _distribution STREQUAL "redistributable"
            AND NOT _distribution STREQUAL "development-only"
            AND NOT _distribution STREQUAL "system"
            AND NOT _distribution STREQUAL "restricted")
        _arken_extpkg_error(
            "arken.external_packages.license.distribution_invalid"
            "license"
            "${recipe_id}"
            "Unsupported distribution classification '${_distribution}'."
        )
    endif()
    string(JSON _component_type ERROR_VARIABLE _component_error TYPE "${recipe_json}" components)
    if(NOT _component_error STREQUAL "NOTFOUND" OR NOT _component_type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.recipe.components_invalid"
            "schema"
            "${recipe_id}"
            "components must be a non-empty array."
        )
    endif()
    string(JSON _component_count LENGTH "${recipe_json}" components)
    if(_component_count EQUAL 0)
        _arken_extpkg_error(
            "arken.external_packages.recipe.components_missing"
            "schema"
            "${recipe_id}"
            "components must be a non-empty array."
        )
    endif()
    set(_component_names "")
    set(_component_targets "")
    math(EXPR _component_last "${_component_count} - 1")
    foreach(_index RANGE 0 ${_component_last})
        string(JSON _component GET "${recipe_json}" components ${_index})
        _arken_extpkg_json_required_string(_component_name "${_component}" "${recipe_id}" name)
        _arken_extpkg_json_required_string(_component_target "${_component}" "${recipe_id}" target)
        list(FIND _component_names "${_component_name}" _name_index)
        list(FIND _component_targets "${_component_target}" _target_index)
        if(NOT _name_index EQUAL -1 OR NOT _target_index EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.recipe.component_duplicate"
                "schema"
                "${recipe_id}"
                "Component names and targets must be unique."
            )
        endif()
        list(APPEND _component_names "${_component_name}")
        list(APPEND _component_targets "${_component_target}")
    endforeach()
    _arken_extpkg_json_array_to_list(_dependencies "${recipe_json}" "${recipe_id}" dependencies)
    _arken_extpkg_require_unique_list("${_dependencies}" "${recipe_id}" dependencies)
    foreach(_dependency IN LISTS _dependencies)
        if(NOT _dependency MATCHES "^[a-z0-9][a-z0-9._-]*-v[0-9]+$")
            _arken_extpkg_error(
                "arken.external_packages.recipe.dependency_invalid"
                "schema"
                "${recipe_id}"
                "Dependency recipe ID is invalid: ${_dependency}"
            )
        endif()
    endforeach()
    _arken_extpkg_json_optional_bool(_test_only "${recipe_json}" FALSE "${recipe_id}" testOnly)
    if(_test_only AND NOT ARKEN_EXTERNAL_PACKAGES_ALLOW_TEST_SOURCES)
        _arken_extpkg_error(
            "arken.external_packages.recipe.test_only_rejected"
            "resolution"
            "${recipe_id}"
            "Test-only recipes require ARKEN_EXTERNAL_PACKAGES_ALLOW_TEST_SOURCES=ON."
        )
    endif()
    set(_ARKEN_EXTPKG_VALIDATED_PACKAGE_ID "${_package_id}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_UPSTREAM_VERSION "${_upstream_version}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_ADAPTER "${_adapter_kind}-v${_adapter_version}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_LICENSE "${_license_spdx}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_DISTRIBUTION "${_distribution}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_COMPONENTS "${_component_names}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_TARGETS "${_component_targets}" PARENT_SCOPE)
    set(_ARKEN_EXTPKG_VALIDATED_DEPENDENCIES "${_dependencies}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_load_recipe recipe_id recipe_root current_platform)
    _arken_extpkg_identifier(_recipe_ident "${recipe_id}")
    get_property(_loaded GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_LOADED_${_recipe_ident}")
    if(_loaded)
        return()
    endif()
    set(_recipe_path "${recipe_root}/${recipe_id}.json")
    if(NOT EXISTS "${_recipe_path}")
        _arken_extpkg_error(
            "arken.external_packages.recipe.missing"
            "resolution"
            "${recipe_id}"
            "Recipe file is missing: ${_recipe_path}"
        )
    endif()
    _arken_extpkg_add_configure_dependency("${_recipe_path}")
    file(READ "${_recipe_path}" _recipe_json)
    file(SHA256 "${_recipe_path}" _recipe_sha)
    _arken_extpkg_validate_recipe(
        "${recipe_id}"
        "${_recipe_path}"
        "${_recipe_json}"
        "${current_platform}"
    )
    set(_package_id "${_ARKEN_EXTPKG_VALIDATED_PACKAGE_ID}")
    _arken_extpkg_identifier(_package_ident "${_package_id}")
    get_property(_existing_recipe GLOBAL PROPERTY "ARKEN_EXTPKG_PACKAGE_RECIPE_${_package_ident}")
    if(NOT "${_existing_recipe}" STREQUAL ""
            AND NOT "${_existing_recipe}" STREQUAL "${recipe_id}")
        _arken_extpkg_error(
            "arken.external_packages.graph.package_conflict"
            "resolution"
            "${_package_id}"
            "Recipes '${_existing_recipe}' and '${recipe_id}' request the same package in one link/process graph. Add a reviewed isolation contract or select one recipe."
        )
    endif()
    foreach(_target IN LISTS _ARKEN_EXTPKG_VALIDATED_TARGETS)
        get_property(_target_recipe GLOBAL PROPERTY "ARKEN_EXTPKG_TARGET_RECIPE_${_target}")
        if(NOT "${_target_recipe}" STREQUAL ""
                AND NOT "${_target_recipe}" STREQUAL "${recipe_id}")
            _arken_extpkg_error(
                "arken.external_packages.graph.target_conflict"
                "resolution"
                "${_target}"
                "Target is declared by both '${_target_recipe}' and '${recipe_id}'."
            )
        endif()
        set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_TARGET_RECIPE_${_target}" "${recipe_id}")
        set_property(GLOBAL APPEND PROPERTY ARKEN_EXTPKG_TARGET_NAMES "${_target}")
    endforeach()
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_PACKAGE_RECIPE_${_package_ident}" "${recipe_id}")
    set_property(GLOBAL APPEND PROPERTY ARKEN_EXTPKG_PACKAGE_IDS "${_package_id}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_LOADED_${_recipe_ident}" TRUE)
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_JSON_${_recipe_ident}" "${_recipe_json}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_PATH_${_recipe_ident}" "${_recipe_path}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SHA_${_recipe_ident}" "${_recipe_sha}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_PACKAGE_${_recipe_ident}" "${_package_id}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_VERSION_${_recipe_ident}" "${_ARKEN_EXTPKG_VALIDATED_UPSTREAM_VERSION}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_ADAPTER_${_recipe_ident}" "${_ARKEN_EXTPKG_VALIDATED_ADAPTER}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_LICENSE_${_recipe_ident}" "${_ARKEN_EXTPKG_VALIDATED_LICENSE}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_DISTRIBUTION_${_recipe_ident}" "${_ARKEN_EXTPKG_VALIDATED_DISTRIBUTION}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_AVAILABLE_COMPONENTS_${_recipe_ident}" "${_ARKEN_EXTPKG_VALIDATED_COMPONENTS}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_DEPENDENCIES_${_recipe_ident}" "${_ARKEN_EXTPKG_VALIDATED_DEPENDENCIES}")
endfunction()

function(_arken_extpkg_verify_requested_components recipe_id)
    _arken_extpkg_identifier(_recipe_ident "${recipe_id}")
    get_property(_requested GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_recipe_ident}")
    get_property(_available GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_AVAILABLE_COMPONENTS_${_recipe_ident}")
    foreach(_component IN LISTS _requested)
        list(FIND _available "${_component}" _index)
        if(_index EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.consumer.component_unknown"
                "resolution"
                "${recipe_id}"
                "Requested component '${_component}' is not declared. Available components: ${_available}"
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_verify_license recipe_json source_root recipe_id)
    _arken_extpkg_json_type(_files_type "${recipe_json}" license files)
    if(_files_type STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.license.files_missing"
            "license"
            "${recipe_id}"
            "license.files must be present, even when it is an empty array for a system SDK."
        )
    endif()
    if(NOT _files_type STREQUAL "ARRAY")
        _arken_extpkg_error(
            "arken.external_packages.license.files_invalid"
            "license"
            "${recipe_id}"
            "license.files must be an array."
        )
    endif()
    string(JSON _length LENGTH "${recipe_json}" license files)
    if(_length EQUAL 0)
        return()
    endif()
    if(source_root STREQUAL "")
        _arken_extpkg_error(
            "arken.external_packages.license.source_missing"
            "license"
            "${recipe_id}"
            "License files require a source root."
        )
    endif()
    math(EXPR _last "${_length} - 1")
    foreach(_index RANGE 0 ${_last})
        string(JSON _record GET "${recipe_json}" license files ${_index})
        _arken_extpkg_validate_object_keys("${_record}" "${recipe_id}:license.files[${_index}]" "path;sha256")
        _arken_extpkg_json_required_string(_relative "${_record}" "${recipe_id}" path)
        _arken_extpkg_json_required_string(_expected "${_record}" "${recipe_id}" sha256)
        if(IS_ABSOLUTE "${_relative}" OR _relative MATCHES "(^|/)\\.\\.(/|$)")
            _arken_extpkg_error(
                "arken.external_packages.license.path_unsafe"
                "license"
                "${recipe_id}"
                "License path is unsafe: ${_relative}"
            )
        endif()
        set(_absolute "${source_root}/${_relative}")
        if(NOT EXISTS "${_absolute}" OR IS_DIRECTORY "${_absolute}" OR IS_SYMLINK "${_absolute}")
            _arken_extpkg_error(
                "arken.external_packages.license.file_missing"
                "license"
                "${recipe_id}"
                "License file is missing: ${_relative}"
            )
        endif()
        _arken_extpkg_add_configure_dependency("${_absolute}")
        file(SHA256 "${_absolute}" _observed)
        if(NOT "${_observed}" STREQUAL "${_expected}")
            _arken_extpkg_error(
                "arken.external_packages.license.hash_mismatch"
                "license"
                "${recipe_id}"
                "License '${_relative}' expected SHA-256 ${_expected}, observed ${_observed}."
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_toolchain_identity out_var)
    set(_parts
        "system=${CMAKE_SYSTEM_NAME}"
        "processor=${CMAKE_SYSTEM_PROCESSOR}"
        "generator=${CMAKE_GENERATOR}"
        "buildType=${CMAKE_BUILD_TYPE}"
        "cCompiler=${CMAKE_C_COMPILER}"
        "cCompilerId=${CMAKE_C_COMPILER_ID}"
        "cCompilerVersion=${CMAKE_C_COMPILER_VERSION}"
        "cxxCompiler=${CMAKE_CXX_COMPILER}"
        "cxxCompilerId=${CMAKE_CXX_COMPILER_ID}"
        "cxxCompilerVersion=${CMAKE_CXX_COMPILER_VERSION}"
        "sysroot=${CMAKE_OSX_SYSROOT}"
        "deploymentTarget=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "architectures=${CMAKE_OSX_ARCHITECTURES}"
    )
    string(JOIN "|" _identity ${_parts})
    set(${out_var} "${_identity}" PARENT_SCOPE)
endfunction()

function(_arken_extpkg_validate_adapter_tool_dependencies recipe_json recipe_id dependencies)
    foreach(_field IN ITEMS makeToolTarget patchToolTarget)
        _arken_extpkg_json_optional_string(
            _tool_target
            "${recipe_json}"
            ""
            "${recipe_id}"
            adapter
            "${_field}"
        )
        if(_tool_target STREQUAL "")
            continue()
        endif()
        get_property(
            _tool_recipe
            GLOBAL
            PROPERTY "ARKEN_EXTPKG_TARGET_RECIPE_${_tool_target}"
        )
        if(_tool_recipe STREQUAL "")
            _arken_extpkg_error(
                "arken.external_packages.adapter.tool_owner_missing"
                "resolution"
                "${recipe_id}"
                "Adapter tool target '${_tool_target}' is not owned by a resolved recipe."
            )
        endif()
        list(FIND dependencies "${_tool_recipe}" _dependency_index)
        if(_dependency_index EQUAL -1)
            _arken_extpkg_error(
                "arken.external_packages.adapter.tool_dependency_missing"
                "resolution"
                "${recipe_id}"
                "Adapter tool target '${_tool_target}' is owned by '${_tool_recipe}', which must be an explicit recipe dependency so its exact instance participates in package identity."
            )
        endif()
    endforeach()
endfunction()

function(_arken_extpkg_resolve_recipe
    recipe_id
    recipe_root
    cache_root
    workspace_root
    current_platform
    package_root
)
    _arken_extpkg_identifier(_recipe_ident "${recipe_id}")
    get_property(_resolved GLOBAL PROPERTY ARKEN_EXTPKG_RESOLVED)
    list(FIND _resolved "${recipe_id}" _resolved_index)
    if(NOT _resolved_index EQUAL -1)
        return()
    endif()
    get_property(_resolving GLOBAL PROPERTY ARKEN_EXTPKG_RESOLVING)
    list(FIND _resolving "${recipe_id}" _resolving_index)
    if(NOT _resolving_index EQUAL -1)
        _arken_extpkg_error(
            "arken.external_packages.graph.cycle"
            "resolution"
            "${recipe_id}"
            "Detected an external package recipe dependency cycle."
        )
    endif()
    set_property(GLOBAL APPEND PROPERTY ARKEN_EXTPKG_RESOLVING "${recipe_id}")
    _arken_extpkg_load_recipe("${recipe_id}" "${recipe_root}" "${current_platform}")
    _arken_extpkg_verify_requested_components("${recipe_id}")
    get_property(_dependencies GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_DEPENDENCIES_${_recipe_ident}")
    set(_dependency_instances "")
    foreach(_dependency IN LISTS _dependencies)
        _arken_extpkg_identifier(_dependency_ident "${_dependency}")
        get_property(_dependency_components GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_dependency_ident}")
        if(_dependency_components STREQUAL "")
            set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_dependency_ident}" "")
        endif()
        _arken_extpkg_resolve_recipe(
            "${_dependency}"
            "${recipe_root}"
            "${cache_root}"
            "${workspace_root}"
            "${current_platform}"
            "${package_root}"
        )
        get_property(_dependency_instance GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_INSTANCE_${_dependency_ident}")
        list(APPEND _dependency_instances "${_dependency_instance}")
    endforeach()
    list(SORT _dependency_instances)
    get_property(_recipe_json GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_JSON_${_recipe_ident}")
    get_property(_recipe_path GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_PATH_${_recipe_ident}")
    get_property(_recipe_sha GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SHA_${_recipe_ident}")
    get_property(_adapter_id GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_ADAPTER_${_recipe_ident}")
    get_property(_distribution GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_DISTRIBUTION_${_recipe_ident}")
    _arken_extpkg_validate_adapter_tool_dependencies(
        "${_recipe_json}"
        "${recipe_id}"
        "${_dependencies}"
    )
    if(DEFINED ARKEN_EXTERNAL_PACKAGES_DISTRIBUTION_BUILD
            AND ARKEN_EXTERNAL_PACKAGES_DISTRIBUTION_BUILD
            AND ("${_distribution}" STREQUAL "development-only"
                OR "${_distribution}" STREQUAL "restricted"))
        _arken_extpkg_error(
            "arken.external_packages.license.distribution_rejected"
            "license"
            "${recipe_id}"
            "Distribution build rejects package classification '${_distribution}'."
        )
    endif()
    if(_distribution STREQUAL "restricted"
            AND (NOT DEFINED ARKEN_EXTERNAL_PACKAGES_ALLOW_RESTRICTED
                OR NOT ARKEN_EXTERNAL_PACKAGES_ALLOW_RESTRICTED))
        _arken_extpkg_error(
            "arken.external_packages.license.restricted_not_admitted"
            "license"
            "${recipe_id}"
            "Restricted package profile requires explicit ARKEN_EXTERNAL_PACKAGES_ALLOW_RESTRICTED=ON."
        )
    endif()
    get_property(_components GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_recipe_ident}")
    get_property(_available_components GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_AVAILABLE_COMPONENTS_${_recipe_ident}")
    if(_components STREQUAL "")
        set(_components "${_available_components}")
    endif()
    list(SORT _components)
    _arken_extpkg_toolchain_identity(_toolchain_identity)
    if(DEFINED ARKEN_EXTERNAL_PACKAGES_OFFLINE AND ARKEN_EXTERNAL_PACKAGES_OFFLINE)
        set(_offline TRUE)
    else()
        set(_offline FALSE)
    endif()
    _arken_extpkg_prepare_source(
        _source_root
        _source_identity
        _source_receipt
        "${_recipe_json}"
        "${_recipe_path}"
        "${recipe_id}"
        "${_recipe_sha}"
        "${cache_root}"
        "${workspace_root}"
        "${_offline}"
    )
    _arken_extpkg_patch_set_identity(
        _patch_set_identity
        "${_recipe_json}"
        "${_recipe_path}"
        "${recipe_id}"
    )
    if(_adapter_id STREQUAL "tool-profile-v1")
        _arken_extpkg_tool_profile_identity(
            _source_identity
            "${_recipe_json}"
            "${recipe_id}"
            "${_components}"
        )
    endif()
    _arken_extpkg_verify_license("${_recipe_json}" "${_source_root}" "${recipe_id}")
    string(SHA256 _instance_id
        "${_recipe_sha}|${_source_identity}|patches=${_patch_set_identity}|${_adapter_id}|${_components}|${_toolchain_identity}|${_dependency_instances}"
    )
    set(_package_build_root "${package_root}/packages/${_instance_id}")
    if(_adapter_id STREQUAL "configure-make-v1")
        set(_receipt_root "${cache_root}/packages/${_instance_id}")
    else()
        set(_receipt_root "${package_root}/receipts/${_instance_id}")
    endif()
    file(MAKE_DIRECTORY "${_package_build_root}")
    if(NOT _adapter_id STREQUAL "configure-make-v1")
        file(MAKE_DIRECTORY "${_receipt_root}")
    endif()
    _arken_extpkg_apply_patches(
        _materialized_source_root
        _materialized_source_identity
        "${_recipe_json}"
        "${_recipe_path}"
        "${recipe_id}"
        "${_source_root}"
        "${_source_identity}"
        "${_package_build_root}"
        "${_patch_set_identity}"
    )
    if(_patch_set_identity STREQUAL "none")
        set(_patch_receipt "")
    else()
        set(_patch_receipt "${_package_build_root}/patched-source.receipt.json")
    endif()
    _arken_extpkg_materialize_recipe(
        "${_recipe_json}"
        "${recipe_id}"
        "${_recipe_sha}"
        "${_instance_id}"
        "${_materialized_source_identity}"
        "${_materialized_source_root}"
        "${_components}"
        "${_package_build_root}"
        "${_receipt_root}"
        "${cache_root}"
    )
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_INSTANCE_${_recipe_ident}" "${_instance_id}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SOURCE_IDENTITY_${_recipe_ident}" "${_materialized_source_identity}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SOURCE_RECEIPT_${_recipe_ident}" "${_source_receipt}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_PATCH_RECEIPT_${_recipe_ident}" "${_patch_receipt}")
    set_property(GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_RECEIPT_ROOT_${_recipe_ident}" "${_receipt_root}")
    get_property(_resolving GLOBAL PROPERTY ARKEN_EXTPKG_RESOLVING)
    list(REMOVE_ITEM _resolving "${recipe_id}")
    set_property(GLOBAL PROPERTY ARKEN_EXTPKG_RESOLVING "${_resolving}")
    set_property(GLOBAL APPEND PROPERTY ARKEN_EXTPKG_RESOLVED "${recipe_id}")
endfunction()

function(_arken_extpkg_generate_receipts graph_receipt spdx_receipt current_platform cache_root package_root)
    get_property(_request_ids GLOBAL PROPERTY ARKEN_EXTPKG_REQUEST_IDS)
    get_property(_recipe_ids GLOBAL PROPERTY ARKEN_EXTPKG_RESOLVED)
    list(REMOVE_DUPLICATES _recipe_ids)

    set(_requests_json "")
    set(_request_index 0)
    foreach(_request_id IN LISTS _request_ids)
        get_property(_enabled GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_ENABLED_${_request_id}")
        get_property(_recipe GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_RECIPE_${_request_id}")
        get_property(_scope GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_SCOPE_${_request_id}")
        get_property(_components GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_COMPONENTS_${_request_id}")
        get_property(_platforms GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_PLATFORMS_${_request_id}")
        get_property(_reason GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_REASON_${_request_id}")
        get_property(_manifest GLOBAL PROPERTY "ARKEN_EXTPKG_REQUEST_MANIFEST_${_request_id}")
        _arken_extpkg_json_escape(_manifest_json "${_manifest}")
        _arken_extpkg_json_escape(_reason_json "${_reason}")
        _arken_extpkg_json_string_array(_components_json "${_components}")
        _arken_extpkg_json_string_array(_platforms_json "${_platforms}")
        if(_request_index GREATER 0)
            string(APPEND _requests_json ",\n")
        endif()
        string(TOLOWER "${_enabled}" _enabled_json)
        string(APPEND _requests_json
            "    {\"requestId\":\"${_request_id}\",\"recipeId\":\"${_recipe}\",\"scope\":\"${_scope}\",\"components\":${_components_json},\"platforms\":${_platforms_json},\"reason\":\"${_reason_json}\",\"enabled\":${_enabled_json},\"manifest\":\"${_manifest_json}\"}"
        )
        math(EXPR _request_index "${_request_index} + 1")
    endforeach()

    set(_packages_json "")
    set(_spdx_packages_json "")
    set(_package_index 0)
    foreach(_recipe IN LISTS _recipe_ids)
        _arken_extpkg_identifier(_recipe_ident "${_recipe}")
        get_property(_package GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_PACKAGE_${_recipe_ident}")
        get_property(_version GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_VERSION_${_recipe_ident}")
        get_property(_recipe_sha GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SHA_${_recipe_ident}")
        get_property(_adapter GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_ADAPTER_${_recipe_ident}")
        get_property(_license GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_LICENSE_${_recipe_ident}")
        get_property(_distribution GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_DISTRIBUTION_${_recipe_ident}")
        get_property(_components GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_COMPONENTS_${_recipe_ident}")
        get_property(_available_components GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_AVAILABLE_COMPONENTS_${_recipe_ident}")
        get_property(_dependencies GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_DEPENDENCIES_${_recipe_ident}")
        if(_components STREQUAL "")
            set(_components "${_available_components}")
        endif()
        _arken_extpkg_json_string_array(_components_json "${_components}")
        _arken_extpkg_json_string_array(_dependencies_json "${_dependencies}")
        get_property(_instance GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_INSTANCE_${_recipe_ident}")
        get_property(_source_identity GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SOURCE_IDENTITY_${_recipe_ident}")
        get_property(_source_receipt GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_SOURCE_RECEIPT_${_recipe_ident}")
        get_property(_patch_receipt GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_PATCH_RECEIPT_${_recipe_ident}")
        get_property(_receipt_root GLOBAL PROPERTY "ARKEN_EXTPKG_RECIPE_RECEIPT_ROOT_${_recipe_ident}")
        _arken_extpkg_json_escape(_source_identity_json "${_source_identity}")
        _arken_extpkg_json_escape(_source_receipt_json "${_source_receipt}")
        _arken_extpkg_json_escape(_patch_receipt_json "${_patch_receipt}")
        _arken_extpkg_json_escape(_receipt_root_json "${_receipt_root}")
        if(_package_index GREATER 0)
            string(APPEND _packages_json ",\n")
            string(APPEND _spdx_packages_json ",\n")
        endif()
        string(APPEND _packages_json
            "    {\"recipeId\":\"${_recipe}\",\"recipeSha256\":\"${_recipe_sha}\",\"packageId\":\"${_package}\",\"upstreamVersion\":\"${_version}\",\"adapterId\":\"${_adapter}\",\"components\":${_components_json},\"dependencies\":${_dependencies_json},\"packageInstanceId\":\"${_instance}\",\"sourceIdentity\":\"${_source_identity_json}\",\"sourceReceipt\":\"${_source_receipt_json}\",\"patchReceipt\":\"${_patch_receipt_json}\",\"receiptRoot\":\"${_receipt_root_json}\",\"license\":\"${_license}\",\"distribution\":\"${_distribution}\"}"
        )
        string(APPEND _spdx_packages_json
            "    {\"SPDXID\":\"SPDXRef-${_recipe_ident}\",\"name\":\"${_package}\",\"versionInfo\":\"${_version}\",\"licenseConcluded\":\"${_license}\",\"licenseDeclared\":\"${_license}\",\"externalRefs\":[{\"referenceCategory\":\"PACKAGE-MANAGER\",\"referenceType\":\"purl\",\"referenceLocator\":\"pkg:generic/${_package}@${_version}?recipe=${_recipe}&instance=${_instance}\"}]}"
        )
        math(EXPR _package_index "${_package_index} + 1")
    endforeach()

    _arken_extpkg_json_escape(_cache_root_json "${cache_root}")
    _arken_extpkg_json_escape(_package_root_json "${package_root}")
    string(CONCAT _graph_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"platform\": \"${current_platform}\",\n"
        "  \"offline\": "
    )
    if(DEFINED ARKEN_EXTERNAL_PACKAGES_OFFLINE AND ARKEN_EXTERNAL_PACKAGES_OFFLINE)
        string(APPEND _graph_json "true,\n")
    else()
        string(APPEND _graph_json "false,\n")
    endif()
    string(APPEND _graph_json
        "  \"cacheRoot\": \"${_cache_root_json}\",\n"
        "  \"materializationRoot\": \"${_package_root_json}\",\n"
        "  \"requests\": [\n${_requests_json}\n  ],\n"
        "  \"packages\": [\n${_packages_json}\n  ]\n"
        "}\n"
    )
    _arken_extpkg_write_if_different("${graph_receipt}" "${_graph_json}")

    string(SHA256 _spdx_package_set_sha "${_spdx_packages_json}")
    string(CONCAT _spdx_json
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"format\": \"spdx-2.3-package-inputs\",\n"
        "  \"packageSetSha256\": \"${_spdx_package_set_sha}\",\n"
        "  \"packages\": [\n${_spdx_packages_json}\n  ]\n"
        "}\n"
    )
    _arken_extpkg_write_if_different("${spdx_receipt}" "${_spdx_json}")
endfunction()

function(arken_resolve_external_packages)
    set(options)
    set(oneValueArgs
        ROOT_MANIFEST
        RECIPE_ROOT
        CACHE_ROOT
        WORKSPACE_ROOT
        OUTPUT_GRAPH_RECEIPT
        OUTPUT_SPDX_INPUT
    )
    set(multiValueArgs MODULE_MANIFESTS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if(NOT ARG_ROOT_MANIFEST OR NOT EXISTS "${ARG_ROOT_MANIFEST}")
        _arken_extpkg_error(
            "arken.external_packages.root_manifest_missing"
            "manifest"
            "root"
            "ROOT_MANIFEST is required."
        )
    endif()
    if(NOT ARG_RECIPE_ROOT)
        get_filename_component(
            ARG_RECIPE_ROOT
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../external_packages/recipes"
            ABSOLUTE
        )
    endif()
    if(NOT ARG_WORKSPACE_ROOT)
        get_filename_component(_root_manifest_dir "${ARG_ROOT_MANIFEST}" DIRECTORY)
        get_filename_component(ARG_WORKSPACE_ROOT "${_root_manifest_dir}/../.." ABSOLUTE)
    endif()
    if(ARG_CACHE_ROOT)
        cmake_path(ABSOLUTE_PATH ARG_CACHE_ROOT NORMALIZE OUTPUT_VARIABLE _cache_root)
    else()
        _arken_extpkg_default_cache_root(_cache_root)
    endif()
    set(_package_root "${CMAKE_BINARY_DIR}/_arken/external-packages")
    if(ARG_OUTPUT_GRAPH_RECEIPT)
        set(_graph_receipt "${ARG_OUTPUT_GRAPH_RECEIPT}")
    else()
        set(_graph_receipt "${_package_root}/external-packages.graph.json")
    endif()
    if(ARG_OUTPUT_SPDX_INPUT)
        set(_spdx_receipt "${ARG_OUTPUT_SPDX_INPUT}")
    else()
        set(_spdx_receipt "${_package_root}/external-packages.spdx-input.json")
    endif()

    _arken_extpkg_reset_graph_state()
    _arken_extpkg_current_platform(_current_platform)
    set(_manifests "${ARG_ROOT_MANIFEST}" ${ARG_MODULE_MANIFESTS})
    list(REMOVE_DUPLICATES _manifests)
    list(SORT _manifests)
    foreach(_manifest IN LISTS _manifests)
        _arken_extpkg_collect_manifest("${_manifest}" "${_current_platform}")
    endforeach()
    get_property(_recipe_ids GLOBAL PROPERTY ARKEN_EXTPKG_RECIPE_IDS)
    list(REMOVE_DUPLICATES _recipe_ids)
    list(SORT _recipe_ids)
    foreach(_recipe IN LISTS _recipe_ids)
        _arken_extpkg_resolve_recipe(
            "${_recipe}"
            "${ARG_RECIPE_ROOT}"
            "${_cache_root}"
            "${ARG_WORKSPACE_ROOT}"
            "${_current_platform}"
            "${_package_root}"
        )
    endforeach()
    _arken_extpkg_generate_receipts(
        "${_graph_receipt}"
        "${_spdx_receipt}"
        "${_current_platform}"
        "${_cache_root}"
        "${_package_root}"
    )
    if(NOT TARGET arken-external-package-inspect)
        add_custom_target(
            arken-external-package-inspect
            COMMAND
                "${CMAKE_COMMAND}"
                "-DGRAPH_RECEIPT=${_graph_receipt}"
                -P "${_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR}/ArkenExternalPackageInspect.cmake"
            VERBATIM
        )
    endif()
    if(NOT TARGET arken-external-package-verify)
        add_custom_target(
            arken-external-package-verify
            COMMAND
                "${CMAKE_COMMAND}"
                "-DRECEIPT_ROOTS_JOINED=${_package_root}/receipts|${_cache_root}/packages"
                -P "${_ARKEN_EXTERNAL_PACKAGE_MODULE_DIR}/ArkenExternalPackageVerifyReceipts.cmake"
            VERBATIM
        )
    endif()
    set(ARKEN_EXTERNAL_PACKAGE_GRAPH_RECEIPT "${_graph_receipt}" PARENT_SCOPE)
    set(ARKEN_EXTERNAL_PACKAGE_SPDX_INPUT "${_spdx_receipt}" PARENT_SCOPE)
endfunction()
