include_guard(GLOBAL)

function(_arken_openssl_read_header_version header_path output_variable)
    file(STRINGS
        "${header_path}"
        _arken_openssl_version_lines
        REGEX "^#[ \t]*define[ \t]+OPENSSL_VERSION_STR[ \t]+\"")
    set(_arken_openssl_header_version "")
    foreach(_arken_openssl_version_line IN LISTS _arken_openssl_version_lines)
        if(_arken_openssl_version_line MATCHES
           "OPENSSL_VERSION_STR[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
            set(_arken_openssl_header_version "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(_arken_openssl_header_version STREQUAL "")
        message(FATAL_ERROR
            "Arken OpenSSL authority could not read OPENSSL_VERSION_STR from "
            "${header_path}")
    endif()
    set(
        ${output_variable}
        "${_arken_openssl_header_version}"
        PARENT_SCOPE)
endfunction()

# TLS consumers share the exact crypto provider; never discover a second SSL
# installation merely because another package exports a similarly named header.
function(arken_configure_openssl_tls_authority)
    arken_configure_openssl_authority()
    find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
    file(REAL_PATH "${OPENSSL_SSL_LIBRARY}" _ssl_library)
    string(FIND "${_ssl_library}/" "${ARKEN_OPENSSL_AUTHORITY_PROVIDER_ROOT}/" _prefix)
    if(NOT _prefix EQUAL 0)
        message(FATAL_ERROR "Arken OpenSSL TLS authority resolved mixed provider roots: provider=${ARKEN_OPENSSL_AUTHORITY_PROVIDER_ROOT} library=${_ssl_library}")
    endif()
    get_target_property(_ssl_imported OpenSSL::SSL IMPORTED_LOCATION)
    file(REAL_PATH "${_ssl_imported}" _ssl_imported)
    if(NOT _ssl_imported STREQUAL _ssl_library)
        message(FATAL_ERROR "Arken OpenSSL TLS target differs from selected provider: expected=${_ssl_library} observed=${_ssl_imported}")
    endif()
    file(SHA256 "${_ssl_library}" _ssl_hash)
    set_property(TARGET OpenSSL::SSL PROPERTY ARKEN_OPENSSL_AUTHORITY_LIBRARY_SHA256 "${_ssl_hash}")
    message(STATUS "Arken OpenSSL TLS authority: library=${_ssl_library} sha256=${_ssl_hash}")
endfunction()

function(arken_configure_openssl_authority)
    if(EMSCRIPTEN)
        return()
    endif()

    if(TARGET OpenSSL::Crypto)
        get_target_property(
            _arken_existing_openssl_authority
            OpenSSL::Crypto
            ARKEN_OPENSSL_AUTHORITY_ID)
        if(NOT _arken_existing_openssl_authority)
            message(FATAL_ERROR
                "OpenSSL::Crypto was configured before the Arken OpenSSL "
                "authority. Configure ArkenOpenSSLAuthority.cmake first so "
                "security-sensitive consumers use one verified provider.")
        endif()
        foreach(_arken_openssl_property IN ITEMS
            ARKEN_OPENSSL_AUTHORITY_ID
            ARKEN_OPENSSL_AUTHORITY_VERSION
            ARKEN_OPENSSL_AUTHORITY_PROVIDER_ROOT
            ARKEN_OPENSSL_AUTHORITY_INCLUDE_ROOT
            ARKEN_OPENSSL_AUTHORITY_CRYPTO_LIBRARY
            ARKEN_OPENSSL_AUTHORITY_HEADER_SHA256
            ARKEN_OPENSSL_AUTHORITY_LIBRARY_SHA256)
            get_target_property(
                _arken_openssl_property_value
                OpenSSL::Crypto
                "${_arken_openssl_property}")
            set(
                "${_arken_openssl_property}"
                "${_arken_openssl_property_value}"
                PARENT_SCOPE)
        endforeach()
        return()
    endif()

    set(_arken_openssl_provider_root "")
    if(APPLE)
        set(_arken_openssl_candidates "")
        if(DEFINED ARKEN_OPENSSL_HOMEBREW_PREFIX AND
           NOT ARKEN_OPENSSL_HOMEBREW_PREFIX STREQUAL "")
            list(APPEND
                _arken_openssl_candidates
                "${ARKEN_OPENSSL_HOMEBREW_PREFIX}")
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            list(APPEND
                _arken_openssl_candidates
                "/opt/homebrew/opt/openssl@3"
                "/usr/local/opt/openssl@3")
        else()
            list(APPEND
                _arken_openssl_candidates
                "/usr/local/opt/openssl@3"
                "/opt/homebrew/opt/openssl@3")
        endif()
        foreach(_arken_openssl_candidate IN LISTS _arken_openssl_candidates)
            if(EXISTS "${_arken_openssl_candidate}/include/openssl/opensslv.h"
               AND EXISTS "${_arken_openssl_candidate}/lib/libcrypto.dylib")
                file(
                    REAL_PATH
                    "${_arken_openssl_candidate}"
                    _arken_openssl_provider_root)
                break()
            endif()
        endforeach()
        if(_arken_openssl_provider_root STREQUAL "")
            message(FATAL_ERROR
                "Arken OpenSSL authority requires the Homebrew openssl@3 "
                "formula on Apple hosts. Install it with 'brew install "
                "openssl@3', or set ARKEN_OPENSSL_HOMEBREW_PREFIX to the "
                "formula's exact prefix before configuring.")
        endif()
        if(NOT _arken_openssl_provider_root MATCHES
           "/Cellar/openssl@3/3\\.[0-9]+\\.[0-9]+$")
            message(FATAL_ERROR
                "Arken OpenSSL authority rejected a provider outside the "
                "Homebrew openssl@3 Cellar: ${_arken_openssl_provider_root}")
        endif()
        set(
            OPENSSL_ROOT_DIR
            "${_arken_openssl_provider_root}"
            CACHE PATH
            "Arken-authorized OpenSSL root"
            FORCE)
    elseif(DEFINED ARKEN_OPENSSL_AUTHORITY_PREFIX AND
           NOT ARKEN_OPENSSL_AUTHORITY_PREFIX STREQUAL "")
        file(
            REAL_PATH
            "${ARKEN_OPENSSL_AUTHORITY_PREFIX}"
            _arken_openssl_provider_root)
        set(
            OPENSSL_ROOT_DIR
            "${_arken_openssl_provider_root}"
            CACHE PATH
            "Arken-authorized OpenSSL root"
            FORCE)
    endif()

    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    if(NOT TARGET OpenSSL::Crypto)
        message(FATAL_ERROR
            "Arken OpenSSL authority requires FindOpenSSL to publish "
            "OpenSSL::Crypto")
    endif()
    if(NOT DEFINED OPENSSL_INCLUDE_DIR OR OPENSSL_INCLUDE_DIR STREQUAL "" OR
       NOT DEFINED OPENSSL_CRYPTO_LIBRARY OR
       OPENSSL_CRYPTO_LIBRARY STREQUAL "")
        message(FATAL_ERROR
            "Arken OpenSSL authority received incomplete FindOpenSSL paths")
    endif()

    file(REAL_PATH "${OPENSSL_INCLUDE_DIR}" _arken_openssl_include_root)
    file(REAL_PATH "${OPENSSL_CRYPTO_LIBRARY}" _arken_openssl_crypto_library)
    set(
        _arken_openssl_header
        "${_arken_openssl_include_root}/openssl/opensslv.h")
    if(NOT EXISTS "${_arken_openssl_header}" OR
       NOT EXISTS "${_arken_openssl_crypto_library}")
        message(FATAL_ERROR
            "Arken OpenSSL authority resolved incomplete provider bytes: "
            "header=${_arken_openssl_header} "
            "library=${_arken_openssl_crypto_library}")
    endif()

    if(_arken_openssl_provider_root STREQUAL "")
        get_filename_component(
            _arken_openssl_provider_root
            "${_arken_openssl_include_root}"
            DIRECTORY)
        file(
            REAL_PATH
            "${_arken_openssl_provider_root}"
            _arken_openssl_provider_root)
    endif()
    foreach(_arken_openssl_owned_path IN ITEMS
            "${_arken_openssl_include_root}"
            "${_arken_openssl_crypto_library}")
        string(
            FIND
            "${_arken_openssl_owned_path}/"
            "${_arken_openssl_provider_root}/"
            _arken_openssl_prefix_index)
        if(NOT _arken_openssl_prefix_index EQUAL 0)
            message(FATAL_ERROR
                "Arken OpenSSL authority resolved mixed provider roots: "
                "provider=${_arken_openssl_provider_root} "
                "path=${_arken_openssl_owned_path}")
        endif()
    endforeach()

    _arken_openssl_read_header_version(
        "${_arken_openssl_header}"
        _arken_openssl_header_version)
    if(_arken_openssl_header_version VERSION_LESS "3.0.0" OR
       NOT _arken_openssl_header_version VERSION_LESS "4.0.0")
        message(FATAL_ERROR
            "Arken OpenSSL authority requires OpenSSL 3.x; found "
            "${_arken_openssl_header_version}")
    endif()
    set(_arken_openssl_package_version "")
    if(DEFINED OpenSSL_VERSION AND NOT OpenSSL_VERSION STREQUAL "")
        set(_arken_openssl_package_version "${OpenSSL_VERSION}")
    elseif(DEFINED OPENSSL_VERSION AND NOT OPENSSL_VERSION STREQUAL "")
        set(_arken_openssl_package_version "${OPENSSL_VERSION}")
    endif()
    if(NOT _arken_openssl_package_version STREQUAL "" AND
       NOT _arken_openssl_package_version STREQUAL
           _arken_openssl_header_version)
        message(FATAL_ERROR
            "Arken OpenSSL authority found a header/package version "
            "mismatch: header=${_arken_openssl_header_version} "
            "package=${_arken_openssl_package_version}")
    endif()

    file(SHA256 "${_arken_openssl_header}" _arken_openssl_header_sha256)
    file(
        SHA256
        "${_arken_openssl_crypto_library}"
        _arken_openssl_library_sha256)
    string(
        CONCAT
        _arken_openssl_authority_input
        "${_arken_openssl_provider_root}\n"
        "${_arken_openssl_header_version}\n"
        "${_arken_openssl_header_sha256}\n"
        "${_arken_openssl_library_sha256}\n")
    string(
        SHA256
        _arken_openssl_authority_digest
        "${_arken_openssl_authority_input}")
    if(APPLE)
        set(_arken_openssl_provider_kind "homebrew")
    else()
        set(_arken_openssl_provider_kind "system")
    endif()
    set(
        _arken_openssl_authority_id
        "arken.openssl.${_arken_openssl_provider_kind}.v1:${_arken_openssl_authority_digest}")

    get_target_property(
        _arken_openssl_target_imported
        OpenSSL::Crypto
        IMPORTED)
    if(NOT _arken_openssl_target_imported)
        message(FATAL_ERROR
            "Arken OpenSSL authority refuses a non-imported OpenSSL::Crypto "
            "target")
    endif()
    foreach(_arken_openssl_property_assignment IN ITEMS
        "ARKEN_OPENSSL_AUTHORITY_ID;${_arken_openssl_authority_id}"
        "ARKEN_OPENSSL_AUTHORITY_VERSION;${_arken_openssl_header_version}"
        "ARKEN_OPENSSL_AUTHORITY_PROVIDER_ROOT;${_arken_openssl_provider_root}"
        "ARKEN_OPENSSL_AUTHORITY_INCLUDE_ROOT;${_arken_openssl_include_root}"
        "ARKEN_OPENSSL_AUTHORITY_CRYPTO_LIBRARY;${_arken_openssl_crypto_library}"
        "ARKEN_OPENSSL_AUTHORITY_HEADER_SHA256;${_arken_openssl_header_sha256}"
        "ARKEN_OPENSSL_AUTHORITY_LIBRARY_SHA256;${_arken_openssl_library_sha256}")
        list(GET _arken_openssl_property_assignment 0 _arken_openssl_property)
        list(GET _arken_openssl_property_assignment 1 _arken_openssl_value)
        set_property(
            TARGET OpenSSL::Crypto
            PROPERTY
                "${_arken_openssl_property}"
                "${_arken_openssl_value}")
        set(
            "${_arken_openssl_property}"
            "${_arken_openssl_value}"
            CACHE INTERNAL
            "Arken OpenSSL authority evidence"
            FORCE)
        set(
            "${_arken_openssl_property}"
            "${_arken_openssl_value}"
            PARENT_SCOPE)
    endforeach()

    message(STATUS
        "Arken OpenSSL authority: "
        "id=${_arken_openssl_authority_id} "
        "version=${_arken_openssl_header_version} "
        "provider=${_arken_openssl_provider_root} "
        "include=${_arken_openssl_include_root} "
        "library=${_arken_openssl_crypto_library} "
        "headerSha256=${_arken_openssl_header_sha256} "
        "librarySha256=${_arken_openssl_library_sha256}")
endfunction()
