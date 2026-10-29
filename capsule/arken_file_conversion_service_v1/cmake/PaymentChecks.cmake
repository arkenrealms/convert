include_guard(GLOBAL)
function(arken_add_conversion_payment_checks)
    get_filename_component(owner "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_executable(arken-conversion-payment-tests
        "${owner}/tests/payment_tests.cpp" "${owner}/tests/payment_recovery_tests.cpp"
        "${owner}/tests/payment_journal_tests.cpp" "${owner}/tests/payment_journal_access_tests.cpp")
    target_compile_features(arken-conversion-payment-tests PRIVATE cxx_std_20)
    target_compile_options(arken-conversion-payment-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(arken-conversion-payment-tests PRIVATE arken_file_conversion_service_v1
        arken_file_conversion_service_v1_payment_journal arken_nlohmann_json_header_only arken_digest_v1)
    add_test(NAME ConversionPayment.Policy COMMAND arken-conversion-payment-tests --write-fixtures "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME ConversionPayment.Version COMMAND arken-conversion-payment-tests --version --invalid)
    set_tests_properties(ConversionPayment.Version PROPERTIES TIMEOUT 5 PASS_REGULAR_EXPRESSION "^arken-conversion-payment-tests r5732")
endfunction()
