#include "arken/identity/v1/c_api.h"

#include "arken/identity/v1/identity.hpp"

#include <algorithm>
#include <array>
#include <new>
#include <span>
#include <string_view>

using arken::identity::v1::ArkenId;
using arken::identity::v1::IdentityError;
using arken::identity::v1::ParseMode;
using arken::identity::v1::V7Capabilities;
using arken::identity::v1::V7Generator;
using arken::identity::v1::V7Options;

struct arken_identity_v7_generator_v1
{
    explicit arken_identity_v7_generator_v1(arken_identity_v7_capabilities_v1 value) noexcept
        : capabilities(value), generator(
            V7Capabilities{this, &read_clock, &fill_random},
            V7Options{value.maximum_clock_rollback_millis})
    {
    }

    static bool read_clock(void* context, std::uint64_t* unix_millis)
    {
        auto* self = static_cast<arken_identity_v7_generator_v1*>(context);
        return self->capabilities.read_clock_millis != nullptr
            && self->capabilities.read_clock_millis(self->capabilities.context, unix_millis) != 0;
    }

    static bool fill_random(void* context, std::uint8_t* output, std::size_t output_size)
    {
        auto* self = static_cast<arken_identity_v7_generator_v1*>(context);
        return self->capabilities.fill_random != nullptr
            && self->capabilities.fill_random(self->capabilities.context, output, output_size) != 0;
    }

    arken_identity_v7_capabilities_v1 capabilities{};
    V7Generator generator;
};

namespace
{
[[nodiscard]] std::uint32_t error_code(IdentityError error) noexcept
{
    return static_cast<std::uint32_t>(error);
}

[[nodiscard]] ArkenId to_cpp(arken_id_v1 const& value) noexcept
{
    ArkenId result{};
    std::copy_n(value.bytes, result.bytes.size(), result.bytes.begin());
    return result;
}

[[nodiscard]] arken_id_v1 to_c(ArkenId value) noexcept
{
    arken_id_v1 result{};
    std::copy(value.bytes.begin(), value.bytes.end(), result.bytes);
    return result;
}
}

extern "C" uint32_t arken_identity_v1_parse(
    char const* text,
    size_t text_size,
    uint32_t parse_mode,
    arken_id_v1* output,
    arken_identity_error_v1* error)
{
    if(error != nullptr)
        *error = {};
    if(text == nullptr || output == nullptr || parse_mode > ARKEN_IDENTITY_PARSE_OPERATOR_INPUT_V1)
    {
        if(error != nullptr)
            error->code = error_code(IdentityError::invalid_argument);
        return error_code(IdentityError::invalid_argument);
    }

    auto const result = arken::identity::v1::parse(
        std::string_view(text, text_size),
        parse_mode == ARKEN_IDENTITY_PARSE_OPERATOR_INPUT_V1
            ? ParseMode::operator_input
            : ParseMode::strict_canonical);
    if(!result)
    {
        if(error != nullptr)
        {
            error->code = error_code(result.error().code);
            error->offset = result.error().offset;
        }
        return error_code(result.error().code);
    }
    *output = to_c(result.value());
    return error_code(IdentityError::none);
}

extern "C" uint32_t arken_identity_v1_format(
    arken_id_v1 const* id,
    char* output,
    size_t output_size,
    size_t* required_size,
    size_t* written_size)
{
    if(required_size != nullptr)
        *required_size = arken::identity::v1::canonical_text_size;
    if(written_size != nullptr)
        *written_size = 0u;
    if(id == nullptr || (output == nullptr && output_size != 0u))
        return error_code(IdentityError::invalid_argument);

    auto const result = arken::identity::v1::format(
        to_cpp(*id),
        std::span<char>(output, output_size));
    if(required_size != nullptr)
        *required_size = result.value().required_size;
    if(!result)
        return error_code(result.error());
    if(written_size != nullptr)
        *written_size = result.value().written_size;
    return error_code(IdentityError::none);
}

extern "C" uint32_t arken_identity_v1_compare(
    arken_id_v1 const* left,
    arken_id_v1 const* right,
    int32_t* comparison)
{
    if(comparison != nullptr)
        *comparison = 0;
    if(left == nullptr || right == nullptr || comparison == nullptr)
        return error_code(IdentityError::invalid_argument);
    auto const ordering = to_cpp(*left) <=> to_cpp(*right);
    if(ordering < 0)
        *comparison = -1;
    else if(ordering > 0)
        *comparison = 1;
    return error_code(IdentityError::none);
}

extern "C" uint32_t arken_identity_v1_validate_issued(arken_id_v1 const* id)
{
    if(id == nullptr)
        return error_code(IdentityError::invalid_argument);
    return error_code(arken::identity::v1::validate_issued(to_cpp(*id)));
}

extern "C" uint32_t arken_identity_v1_uuid_v7_unix_millis(
    arken_id_v1 const* id,
    uint64_t* unix_millis)
{
    if(unix_millis != nullptr)
        *unix_millis = 0u;
    if(id == nullptr || unix_millis == nullptr)
        return error_code(IdentityError::invalid_argument);
    ArkenId const value = to_cpp(*id);
    if(value.version() != 7u)
        return error_code(IdentityError::unsupported_version);
    if(!value.has_rfc_variant())
        return error_code(IdentityError::invalid_variant);
    *unix_millis = arken::identity::v1::uuid_v7_unix_millis(value);
    return error_code(IdentityError::none);
}

extern "C" uint32_t arken_identity_v1_v7_generator_create(
    arken_identity_v7_capabilities_v1 const* capabilities,
    arken_identity_v7_generator_v1** output)
{
    if(output != nullptr)
        *output = nullptr;
    if(capabilities == nullptr || output == nullptr
        || capabilities->read_clock_millis == nullptr
        || capabilities->fill_random == nullptr)
    {
        return error_code(IdentityError::invalid_argument);
    }
    auto* generator = new(std::nothrow) arken_identity_v7_generator_v1(*capabilities);
    if(generator == nullptr)
        return error_code(IdentityError::allocation_failed);
    *output = generator;
    return error_code(IdentityError::none);
}

extern "C" void arken_identity_v1_v7_generator_destroy(arken_identity_v7_generator_v1* generator)
{
    delete generator;
}

extern "C" uint32_t arken_identity_v1_v7_generator_next(
    arken_identity_v7_generator_v1* generator,
    arken_id_v1* output)
{
    if(generator == nullptr || output == nullptr)
        return error_code(IdentityError::invalid_argument);
    auto const result = generator->generator.next();
    if(!result)
        return error_code(result.error());
    *output = to_c(result.value());
    return error_code(IdentityError::none);
}

extern "C" uint32_t arken_identity_v1_v7_generator_fill(
    arken_identity_v7_generator_v1* generator,
    arken_id_v1* output,
    size_t output_count,
    size_t* written_count)
{
    if(written_count != nullptr)
        *written_count = 0u;
    if(generator == nullptr || (output == nullptr && output_count != 0u))
        return error_code(IdentityError::invalid_argument);

    constexpr std::size_t batch_capacity = 256u;
    std::array<ArkenId, batch_capacity> batch{};
    std::size_t output_offset = 0u;
    while(output_offset < output_count)
    {
        std::size_t const requested = std::min(batch.size(), output_count - output_offset);
        auto const result = generator->generator.fill(
            std::span<ArkenId>(batch.data(), requested));
        std::size_t const produced = result.value();
        for(std::size_t index = 0u; index < produced; ++index)
            output[output_offset + index] = to_c(batch[index]);
        output_offset += produced;
        if(!result)
        {
            if(written_count != nullptr)
                *written_count = output_offset;
            return error_code(result.error());
        }
    }
    if(written_count != nullptr)
        *written_count = output_offset;
    return error_code(IdentityError::none);
}
