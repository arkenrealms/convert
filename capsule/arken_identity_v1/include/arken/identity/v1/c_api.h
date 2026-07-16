#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arken_id_v1 {
    uint8_t bytes[16];
} arken_id_v1;

typedef struct arken_identity_error_v1 {
    uint32_t code;
    uint32_t offset;
} arken_identity_error_v1;

typedef int (*arken_identity_clock_millis_fn_v1)(void* context, uint64_t* unix_millis);
typedef int (*arken_identity_random_fill_fn_v1)(void* context, uint8_t* output, size_t output_size);

typedef struct arken_identity_v7_capabilities_v1 {
    void* context;
    arken_identity_clock_millis_fn_v1 read_clock_millis;
    arken_identity_random_fill_fn_v1 fill_random;
    uint64_t maximum_clock_rollback_millis;
} arken_identity_v7_capabilities_v1;

typedef struct arken_identity_v7_generator_v1 arken_identity_v7_generator_v1;

enum {
    ARKEN_IDENTITY_PARSE_STRICT_CANONICAL_V1 = 0,
    ARKEN_IDENTITY_PARSE_OPERATOR_INPUT_V1 = 1,
    ARKEN_IDENTITY_CANONICAL_TEXT_SIZE_V1 = 36
};

uint32_t arken_identity_v1_parse(
    char const* text,
    size_t text_size,
    uint32_t parse_mode,
    arken_id_v1* output,
    arken_identity_error_v1* error);

uint32_t arken_identity_v1_format(
    arken_id_v1 const* id,
    char* output,
    size_t output_size,
    size_t* required_size,
    size_t* written_size);

uint32_t arken_identity_v1_compare(
    arken_id_v1 const* left,
    arken_id_v1 const* right,
    int32_t* comparison);
uint32_t arken_identity_v1_validate_issued(arken_id_v1 const* id);
uint32_t arken_identity_v1_uuid_v7_unix_millis(
    arken_id_v1 const* id,
    uint64_t* unix_millis);

uint32_t arken_identity_v1_v7_generator_create(
    arken_identity_v7_capabilities_v1 const* capabilities,
    arken_identity_v7_generator_v1** output);

void arken_identity_v1_v7_generator_destroy(arken_identity_v7_generator_v1* generator);

uint32_t arken_identity_v1_v7_generator_next(
    arken_identity_v7_generator_v1* generator,
    arken_id_v1* output);

uint32_t arken_identity_v1_v7_generator_fill(
    arken_identity_v7_generator_v1* generator,
    arken_id_v1* output,
    size_t output_count,
    size_t* written_count);

#ifdef __cplusplus
}
#endif
