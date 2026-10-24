#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include "arken_file_conversion_service_v1/server_surface.hpp"
namespace arken::file_conversion::service::v1 {
// C++ decodes the envelope and generated command models. The adapter receives
// an execution plan with an exact serialized response prefix, preserving even
// JSON-RPC numeric IDs that JavaScript Number cannot represent exactly.
[[nodiscard]] std::string PlanRequest(std::string_view json,ServerMode mode=ServerMode::preview) noexcept;
[[nodiscard]] std::string Catalog();
// Private completion metadata only; malformed/oversized input returns null.
// Does not plan or execute a command, inspect storage, or admit payment.
[[nodiscard]] std::string CallSummary(std::string_view request, int httpStatus) noexcept;
// Private completion metadata; never part of the public JSON-RPC response.
[[nodiscard]] std::string CostContextPacket(std::string_view packet) noexcept;
// Closed private codec-execution observation. No request, file or payment data.
// A codec success does not assert storage, settlement or download completion.
[[nodiscard]] std::string ConversionObservation(std::uint32_t input, std::uint32_t output,
    std::uint32_t error, std::uint32_t decodeError, std::uint32_t encodeError) noexcept;
// Native admission against the immutable upload options, before any payment or
// result lookup. Pair tools share the generic file/quote identity, not a new job.
[[nodiscard]] std::string ConversionTargetPacket(std::string_view json) noexcept;
}
