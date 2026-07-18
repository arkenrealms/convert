include_guard(GLOBAL)

set(ARKEN_NLOHMANN_JSON_VERSION "3.12.0")
set(ARKEN_NLOHMANN_JSON_UPSTREAM_COMMIT "f8eee1bb7953c6a4bff384d45052d5acc3d69698")
set(ARKEN_NLOHMANN_JSON_SHA256 "acaa0c0e8cb75bbb2001ef3312549140f0ede093cf6f772683b612db75ecd004")
set(ARKEN_NLOHMANN_JSON_FWD_SHA256 "93d2c8f52015dfc200249a42a94c488df73c3c1a73139915ae66d34e314e656d")

function(arken_configure_pinned_nlohmann_json)
    get_filename_component(
        _arken_nlohmann_sdk_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.."
        ABSOLUTE
    )
    set(_arken_nlohmann_include_dir "${_arken_nlohmann_sdk_root}/third_party")
    set(_arken_nlohmann_header "${_arken_nlohmann_include_dir}/nlohmann/json.hpp")
    set(_arken_nlohmann_fwd_header "${_arken_nlohmann_include_dir}/nlohmann/json_fwd.hpp")

    if(NOT EXISTS "${_arken_nlohmann_header}" OR NOT EXISTS "${_arken_nlohmann_fwd_header}")
        message(FATAL_ERROR
            "The tracked Arken SDK nlohmann dependency is incomplete under "
            "${_arken_nlohmann_include_dir}/nlohmann. "
            "Restore the source-controlled SDK third_party directory; do not copy a header from "
            "another worktree or use an ignored external/ directory."
        )
    endif()

    file(SHA256 "${_arken_nlohmann_header}" _arken_nlohmann_sha256)
    if(NOT _arken_nlohmann_sha256 STREQUAL ARKEN_NLOHMANN_JSON_SHA256)
        message(FATAL_ERROR
            "The tracked Arken SDK nlohmann ${ARKEN_NLOHMANN_JSON_VERSION} snapshot "
            "(${ARKEN_NLOHMANN_JSON_UPSTREAM_COMMIT}) has an "
            "unexpected SHA-256 (${_arken_nlohmann_sha256}); expected "
            "${ARKEN_NLOHMANN_JSON_SHA256}. Update the pinned version and hash together."
        )
    endif()
    file(SHA256 "${_arken_nlohmann_fwd_header}" _arken_nlohmann_fwd_sha256)
    if(NOT _arken_nlohmann_fwd_sha256 STREQUAL ARKEN_NLOHMANN_JSON_FWD_SHA256)
        message(FATAL_ERROR
            "The tracked Arken SDK nlohmann json_fwd.hpp snapshot "
            "(${ARKEN_NLOHMANN_JSON_UPSTREAM_COMMIT}) has an unexpected SHA-256 "
            "(${_arken_nlohmann_fwd_sha256}); expected ${ARKEN_NLOHMANN_JSON_FWD_SHA256}. "
            "Update the pinned commit and both hashes together."
        )
    endif()

    if(TARGET nlohmann_json::nlohmann_json AND NOT TARGET arken_nlohmann_json_header_only)
        message(FATAL_ERROR
            "A different nlohmann_json::nlohmann_json target was configured before the tracked "
            "Arken SDK dependency. Configure ArkenPinnedNlohmannJson.cmake first so one header "
            "authority is used throughout the process."
        )
    endif()
    if(NOT TARGET arken_nlohmann_json_header_only)
        add_library(arken_nlohmann_json_header_only INTERFACE)
        target_include_directories(
            arken_nlohmann_json_header_only
            BEFORE INTERFACE "${_arken_nlohmann_include_dir}"
        )
        target_compile_features(arken_nlohmann_json_header_only INTERFACE cxx_std_11)
    endif()
    if(NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json ALIAS arken_nlohmann_json_header_only)
    endif()

    set(ARKEN_NLOHMANN_INCLUDE_DIR "${_arken_nlohmann_include_dir}" PARENT_SCOPE)
    set(ARKEN_NLOHMANN_TARGET "nlohmann_json::nlohmann_json" PARENT_SCOPE)
endfunction()
