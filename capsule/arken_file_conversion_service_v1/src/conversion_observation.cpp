#include "arken_file_conversion_service_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "arken_schema_cpp_v1/tool_json.hpp"

namespace arken::file_conversion::service::v1 {
namespace {
namespace C = arken::file_conversion::v1;
namespace I = arken::image::v1;
std::string_view Format(std::uint32_t value) {
    switch(static_cast<I::Format>(value)) {
    case I::Format::pcx:return "pcx"; case I::Format::cel:return "cel";
    case I::Format::dc6:return "dc6"; case I::Format::blp:return "blp";
    case I::Format::dds:return "dds"; case I::Format::tga:return "tga";
    case I::Format::png:return "png"; case I::Format::bmp:return "bmp";
    case I::Format::jpeg:return "jpeg"; case I::Format::gif:return "gif";
    }
    return "unknown";
}
std::string_view Reason(C::Error error,I::DecodeError decode,I::EncodeError encode) {
    switch(error) {
    case C::Error::none:return "none";
    case C::Error::input_limit:return "input_limit";
    case C::Error::input_format_unavailable:return "unsupported_input";
    case C::Error::output_format_unavailable:return "unsupported_output";
    case C::Error::invalid_options:return "invalid_options";
    case C::Error::allocation_failed:return "allocation_failed";
    case C::Error::internal_failure:case C::Error::input_not_prepared:return "internal_failure";
    case C::Error::decode_failed:
        switch(decode) {
        case I::DecodeError::unsupported_content:case I::DecodeError::embedded_jpeg:return "unsupported_content";
        case I::DecodeError::unsupported_compression:return "unsupported_compression";
        case I::DecodeError::unsupported_alpha_depth:return "unsupported_alpha";
        case I::DecodeError::input_too_large:return "input_limit";
        case I::DecodeError::resource_limit:case I::DecodeError::integer_overflow:return "resource_limit";
        case I::DecodeError::allocation_failed:return "allocation_failed";
        default:return "invalid_file";
        }
    case C::Error::encode_failed:
        switch(encode) {
        case I::EncodeError::unsupported_format:return "unsupported_output";
        case I::EncodeError::alpha_requires_matte:return "alpha_policy";
        case I::EncodeError::output_limit:return "output_limit";
        case I::EncodeError::invalid_options:return "invalid_options";
        case I::EncodeError::allocation_failed:return "allocation_failed";
        default:return "encode_failed";
        }
    }
    return "internal_failure";
}
}
std::string ConversionObservation(std::uint32_t input,std::uint32_t output,
    std::uint32_t error,std::uint32_t decodeError,std::uint32_t encodeError) noexcept {
    try {
        if(error>static_cast<unsigned>(C::Error::input_not_prepared)||
           decodeError>static_cast<unsigned>(I::DecodeError::embedded_jpeg)||
           encodeError>static_cast<unsigned>(I::EncodeError::encoding_failed))return "null";
        const auto status=static_cast<C::Error>(error);
        if((status==C::Error::decode_failed)!=(decodeError!=0)||
           (status==C::Error::encode_failed)!=(encodeError!=0))return "null";
        return arken::schema::cpp::v1::tool_json::Json{
            {"schema","arken.mcp.conversion.v1"},{"service","convert"},
            {"inputFormat",Format(input)},{"outputFormat",Format(output)},
            {"outcome",error==0?"succeeded":"failed"},
            {"reason",Reason(status,static_cast<I::DecodeError>(decodeError),static_cast<I::EncodeError>(encodeError))}}.dump();
    } catch(...) {return "null";}
}
}
