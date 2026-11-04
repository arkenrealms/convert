if(DEFINED RELEASE_EVIDENCE
        AND NOT RELEASE_EVIDENCE STREQUAL ""
        AND EXISTS "${RELEASE_EVIDENCE}")
    file(READ "${RELEASE_EVIDENCE}" _receipt)
    message("${_receipt}")
    return()
endif()
if(NOT DEFINED GRAPH_RECEIPT
        OR GRAPH_RECEIPT STREQUAL ""
        OR NOT EXISTS "${GRAPH_RECEIPT}")
    message(FATAL_ERROR
        "RELEASE_EVIDENCE or GRAPH_RECEIPT must identify a generated external package receipt")
endif()
file(READ "${GRAPH_RECEIPT}" _internal_receipt)
message("${_internal_receipt}")
