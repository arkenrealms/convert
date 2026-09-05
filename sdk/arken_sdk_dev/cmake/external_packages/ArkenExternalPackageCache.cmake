include_guard(GLOBAL)

# Compiler recipes may not inherit host-global mutable acquisition/build state.
# The recipe, not a consumer's CACHE_ROOT override, owns this isolation policy.
function(_arken_extpkg_recipe_cache_root out_root out_scope recipe_json recipe_id base_root workspace_root)
    _arken_extpkg_json_optional_string(_scope "${recipe_json}" "shared" "${recipe_id}" cacheScope)
    if(_scope STREQUAL "shared")
        set(_root "${base_root}")
    elseif(_scope STREQUAL "workspace")
        if(NOT IS_ABSOLUTE "${workspace_root}" OR NOT IS_DIRECTORY "${workspace_root}")
            _arken_extpkg_error("arken.external_packages.cache.workspace_required"
                "resolution" "${recipe_id}" "Workspace-scoped packages require an existing absolute workspace root.")
        endif()
        file(REAL_PATH "${workspace_root}" _workspace)
        set(_root "${_workspace}")
        # REAL_PATH on a missing leaf does not reliably resolve existing parent
        # symlinks. Resolve each existing ancestor before appending the next one.
        foreach(_part IN ITEMS .tmp arken-external-packages v1)
            set(_next "${_root}/${_part}")
            if(EXISTS "${_next}" OR IS_SYMLINK "${_next}")
                file(REAL_PATH "${_next}" _root)
            else()
                set(_root "${_next}")
            endif()
            cmake_path(IS_PREFIX _workspace "${_root}" NORMALIZE _contained)
            if(NOT _contained)
                _arken_extpkg_error("arken.external_packages.cache.workspace_escape"
                    "resolution" "${recipe_id}" "Workspace package cache must remain inside its physical workspace.")
            endif()
        endforeach()
    else()
        _arken_extpkg_error("arken.external_packages.cache.scope_invalid"
            "schema" "${recipe_id}" "cacheScope must be shared or workspace.")
    endif()
    set(${out_root} "${_root}" PARENT_SCOPE)
    set(${out_scope} "${_scope}" PARENT_SCOPE)
endfunction()
