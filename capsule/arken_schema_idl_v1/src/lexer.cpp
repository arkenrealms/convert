#include "lexer.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace arken::schema::idl::v1::detail
{
namespace
{
bool IsIdentifierStart(char value)
{
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') || value == '_';
}

bool IsIdentifierContinue(char value)
{
    return IsIdentifierStart(value) || (value >= '0' && value <= '9');
}

bool IsHex(char value)
{
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

std::uint32_t HexValue(char value)
{
    if (value >= '0' && value <= '9') return static_cast<std::uint32_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint32_t>(value - 'a' + 10);
    return static_cast<std::uint32_t>(value - 'A' + 10);
}

void AppendUtf8(std::string& output, std::uint32_t code_point)
{
    if (code_point <= 0x7f)
    {
        output.push_back(static_cast<char>(code_point));
    }
    else if (code_point <= 0x7ff)
    {
        output.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
    else if (code_point <= 0xffff)
    {
        output.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
    else
    {
        output.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
}

bool IsContinuation(unsigned char value)
{
    return value >= 0x80 && value <= 0xbf;
}

std::optional<std::size_t> InvalidUtf8Offset(std::string_view source)
{
    std::size_t offset = 0;
    while (offset < source.size())
    {
        const auto first = static_cast<unsigned char>(source[offset]);
        if (first <= 0x7f)
        {
            ++offset;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf)
        {
            if (offset + 1 >= source.size() ||
                !IsContinuation(static_cast<unsigned char>(source[offset + 1])))
                return offset;
            offset += 2;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef)
        {
            if (offset + 2 >= source.size()) return offset;
            const auto second = static_cast<unsigned char>(source[offset + 1]);
            const auto third = static_cast<unsigned char>(source[offset + 2]);
            const bool second_valid = first == 0xe0
                ? second >= 0xa0 && second <= 0xbf
                : first == 0xed
                    ? second >= 0x80 && second <= 0x9f
                    : IsContinuation(second);
            if (!second_valid || !IsContinuation(third)) return offset;
            offset += 3;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4)
        {
            if (offset + 3 >= source.size()) return offset;
            const auto second = static_cast<unsigned char>(source[offset + 1]);
            const auto third = static_cast<unsigned char>(source[offset + 2]);
            const auto fourth = static_cast<unsigned char>(source[offset + 3]);
            const bool second_valid = first == 0xf0
                ? second >= 0x90 && second <= 0xbf
                : first == 0xf4
                    ? second >= 0x80 && second <= 0x8f
                    : IsContinuation(second);
            if (!second_valid || !IsContinuation(third) || !IsContinuation(fourth)) return offset;
            offset += 4;
            continue;
        }
        return offset;
    }
    return std::nullopt;
}

std::size_t Utf8SequenceLength(unsigned char first)
{
    if (first <= 0x7f) return 1;
    if (first <= 0xdf) return 2;
    if (first <= 0xef) return 3;
    return 4;
}

SourcePosition PositionAt(std::string_view source, std::size_t target)
{
    SourcePosition position;
    std::size_t offset = 0;
    while (offset < std::min(target, source.size()))
    {
        const unsigned char value = static_cast<unsigned char>(source[offset]);
        if (value == '\r')
        {
            if (offset + 1 < source.size() && source[offset + 1] == '\n') ++offset;
            ++position.line;
            position.column = 1;
            ++offset;
            continue;
        }
        if (value == '\n')
        {
            ++position.line;
            position.column = 1;
            ++offset;
            continue;
        }
        offset += Utf8SequenceLength(value);
        ++position.column;
    }
    position.offset = target;
    return position;
}

TokenKind KeywordKind(std::string_view value)
{
    if (value == "edition") return TokenKind::Edition;
    if (value == "schema") return TokenKind::Schema;
    if (value == "owner") return TokenKind::Owner;
    if (value == "import") return TokenKind::Import;
    if (value == "model") return TokenKind::Model;
    if (value == "primary_id") return TokenKind::PrimaryId;
    if (value == "field") return TokenKind::Field;
    if (value == "reserved") return TokenKind::Reserved;
    return TokenKind::Identifier;
}

class Lexer
{
public:
    Lexer(std::string_view source_name, std::string_view source, const ParseLimits& limits)
        : sourceName_(source_name), source_(source), limits_(limits)
    {
    }

    LexResult Run()
    {
        if (source_.size() > limits_.maximumInputBytes)
        {
            AddDiagnostic(
                "idl.input_too_large",
                "IDL input exceeds the configured byte limit.",
                {Position(), Position()}
            );
            AddEnd();
            return std::move(result_);
        }
        if (const auto invalid = InvalidUtf8Offset(source_); invalid.has_value())
        {
            const auto begin = PositionAt(source_, *invalid);
            auto end = begin;
            end.offset = std::min(*invalid + 1, source_.size());
            ++end.column;
            AddDiagnostic(
                "idl.invalid_utf8",
                "IDL source must be valid UTF-8.",
                {begin, end}
            );
            AddEnd();
            return std::move(result_);
        }

        while (!AtEnd() && !tokenLimitReached_)
        {
            SkipWhitespace();
            if (AtEnd()) break;
            const SourcePosition begin = Position();
            const char value = Peek();
            if (value == '/' && Peek(1) == '/')
            {
                LexComment(begin);
            }
            else if (IsIdentifierStart(value))
            {
                LexIdentifier(begin);
            }
            else if (value >= '0' && value <= '9')
            {
                LexInteger(begin);
            }
            else if (value == '"')
            {
                LexString(begin);
            }
            else
            {
                LexPunctuation(begin);
            }
        }
        AddEnd();
        return std::move(result_);
    }

private:
    bool AtEnd() const
    {
        return offset_ >= source_.size();
    }

    char Peek(std::size_t lookahead = 0) const
    {
        return offset_ + lookahead < source_.size() ? source_[offset_ + lookahead] : '\0';
    }

    SourcePosition Position() const
    {
        return {.offset = offset_, .line = line_, .column = column_};
    }

    void AdvanceAscii()
    {
        if (AtEnd()) return;
        const char value = source_[offset_++];
        if (value == '\n' || value == '\r')
        {
            ++line_;
            column_ = 1;
        }
        else
        {
            ++column_;
        }
    }

    void AdvanceUtf8()
    {
        if (AtEnd()) return;
        const std::size_t length = Utf8SequenceLength(static_cast<unsigned char>(source_[offset_]));
        offset_ += std::min(length, source_.size() - offset_);
        ++column_;
    }

    void SkipWhitespace()
    {
        while (!AtEnd())
        {
            const char value = Peek();
            if (value == ' ' || value == '\t' || value == '\f')
            {
                AdvanceAscii();
                continue;
            }
            if (value == '\r')
            {
                if (Peek(1) == '\n')
                {
                    offset_ += 2;
                    ++line_;
                    column_ = 1;
                }
                else
                {
                    AdvanceAscii();
                }
                continue;
            }
            if (value == '\n')
            {
                AdvanceAscii();
                continue;
            }
            break;
        }
    }

    void AddToken(
        TokenKind kind,
        SourcePosition begin,
        std::size_t text_begin,
        std::string decoded_text = {}
    )
    {
        const std::size_t text_size = offset_ - text_begin;
        if (text_size > limits_.maximumTokenBytes)
        {
            AddDiagnostic(
                "idl.token_too_long",
                "IDL token exceeds the configured byte limit.",
                {begin, Position()}
            );
            tokenLimitReached_ = true;
            return;
        }
        if (result_.tokens.size() >= limits_.maximumTokens)
        {
            AddDiagnostic(
                "idl.token_limit_exceeded",
                "IDL token count exceeds the configured limit.",
                {begin, Position()}
            );
            tokenLimitReached_ = true;
            return;
        }
        result_.tokens.push_back({
            .kind = kind,
            .text = std::string(source_.substr(text_begin, text_size)),
            .decodedText = std::move(decoded_text),
            .span = {begin, Position()},
        });
    }

    void AddEnd()
    {
        const auto position = Position();
        result_.tokens.push_back({
            .kind = TokenKind::End,
            .text = {},
            .decodedText = {},
            .span = {position, position},
        });
    }

    void AddDiagnostic(std::string code, std::string message, SourceSpan span)
    {
        if (result_.diagnostics.size() >= limits_.maximumDiagnostics) return;
        result_.diagnostics.push_back({
            .code = std::move(code),
            .severity = DiagnosticSeverity::Error,
            .message = std::move(message),
            .sourceName = sourceName_,
            .span = span,
        });
    }

    void LexIdentifier(SourcePosition begin)
    {
        const std::size_t text_begin = offset_;
        while (!AtEnd() && IsIdentifierContinue(Peek())) AdvanceAscii();
        const auto text = source_.substr(text_begin, offset_ - text_begin);
        AddToken(KeywordKind(text), begin, text_begin);
    }

    void LexInteger(SourcePosition begin)
    {
        const std::size_t text_begin = offset_;
        if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X'))
        {
            AdvanceAscii();
            AdvanceAscii();
            const std::size_t digits_begin = offset_;
            while (!AtEnd() && IsHex(Peek())) AdvanceAscii();
            if (offset_ == digits_begin)
            {
                AddDiagnostic(
                    "idl.invalid_integer_literal",
                    "Hexadecimal integer literals require at least one digit.",
                    {begin, Position()}
                );
            }
        }
        else
        {
            while (!AtEnd() && Peek() >= '0' && Peek() <= '9') AdvanceAscii();
        }
        AddToken(TokenKind::Integer, begin, text_begin);
    }

    std::optional<std::uint32_t> ConsumeHexQuad()
    {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            if (AtEnd() || !IsHex(Peek())) return std::nullopt;
            value = (value << 4) | HexValue(Peek());
            AdvanceAscii();
        }
        return value;
    }

    void LexString(SourcePosition begin)
    {
        const std::size_t text_begin = offset_;
        AdvanceAscii();
        bool terminated = false;
        bool size_diagnostic_added = false;
        std::string decoded;
        decoded.reserve(std::min(limits_.maximumStringBytes, source_.size() - text_begin));
        while (!AtEnd())
        {
            if (Peek() == '"')
            {
                AdvanceAscii();
                terminated = true;
                break;
            }
            if (Peek() == '\r' || Peek() == '\n') break;
            if (static_cast<unsigned char>(Peek()) < 0x20)
            {
                const auto control_begin = Position();
                AdvanceAscii();
                AddDiagnostic(
                    "idl.invalid_string_control",
                    "String literals cannot contain unescaped control characters.",
                    {control_begin, Position()}
                );
                continue;
            }
            if (Peek() == '\\')
            {
                const auto escape_begin = Position();
                AdvanceAscii();
                if (AtEnd()) break;
                const char escape = Peek();
                if (escape == 'u')
                {
                    AdvanceAscii();
                    const auto first = ConsumeHexQuad();
                    if (!first.has_value())
                    {
                        AddDiagnostic(
                            "idl.invalid_string_escape",
                            "Unicode string escapes require four hexadecimal digits.",
                            {escape_begin, Position()}
                        );
                    }
                    else if (*first >= 0xd800 && *first <= 0xdbff)
                    {
                        if (Peek() != '\\' || Peek(1) != 'u')
                        {
                            AddDiagnostic(
                                "idl.invalid_string_surrogate",
                                "A high surrogate must be followed by a low surrogate.",
                                {escape_begin, Position()}
                            );
                        }
                        else
                        {
                            AdvanceAscii();
                            AdvanceAscii();
                            const auto second = ConsumeHexQuad();
                            if (!second.has_value() || *second < 0xdc00 || *second > 0xdfff)
                            {
                                AddDiagnostic(
                                    "idl.invalid_string_surrogate",
                                    "A high surrogate must be followed by a low surrogate.",
                                    {escape_begin, Position()}
                                );
                            }
                            else
                            {
                                const std::uint32_t code_point = 0x10000 +
                                    ((*first - 0xd800) << 10) + (*second - 0xdc00);
                                AppendUtf8(decoded, code_point);
                            }
                        }
                    }
                    else if (*first >= 0xdc00 && *first <= 0xdfff)
                    {
                        AddDiagnostic(
                            "idl.invalid_string_surrogate",
                            "A low surrogate cannot appear without a preceding high surrogate.",
                            {escape_begin, Position()}
                        );
                    }
                    else
                    {
                        AppendUtf8(decoded, *first);
                    }
                }
                else if (std::string_view("\"\\/bfnrt").find(escape) != std::string_view::npos)
                {
                    AdvanceAscii();
                    switch (escape)
                    {
                        case '"': decoded.push_back('"'); break;
                        case '\\': decoded.push_back('\\'); break;
                        case '/': decoded.push_back('/'); break;
                        case 'b': decoded.push_back('\b'); break;
                        case 'f': decoded.push_back('\f'); break;
                        case 'n': decoded.push_back('\n'); break;
                        case 'r': decoded.push_back('\r'); break;
                        case 't': decoded.push_back('\t'); break;
                        default: break;
                    }
                }
                else
                {
                    AdvanceAscii();
                    AddDiagnostic(
                        "idl.invalid_string_escape",
                        "String literal contains an unsupported escape.",
                        {escape_begin, Position()}
                    );
                }
                continue;
            }
            const std::size_t value_begin = offset_;
            if (static_cast<unsigned char>(Peek()) > 0x7f) AdvanceUtf8();
            else AdvanceAscii();
            decoded.append(source_.substr(value_begin, offset_ - value_begin));
            if (!size_diagnostic_added && decoded.size() > limits_.maximumStringBytes)
            {
                AddDiagnostic(
                    "idl.string_too_long",
                    "Decoded string literal exceeds the configured byte limit.",
                    {begin, Position()}
                );
                size_diagnostic_added = true;
            }
        }
        if (!size_diagnostic_added && decoded.size() > limits_.maximumStringBytes)
        {
            AddDiagnostic(
                "idl.string_too_long",
                "Decoded string literal exceeds the configured byte limit.",
                {begin, Position()}
            );
        }
        if (!terminated)
        {
            AddDiagnostic(
                "idl.unterminated_string",
                "String literal must terminate before the end of its line.",
                {begin, Position()}
            );
        }
        AddToken(TokenKind::String, begin, text_begin, std::move(decoded));
    }

    void LexComment(SourcePosition begin)
    {
        const std::size_t text_begin = offset_;
        AdvanceAscii();
        AdvanceAscii();
        while (!AtEnd() && Peek() != '\r' && Peek() != '\n')
        {
            if (static_cast<unsigned char>(Peek()) > 0x7f) AdvanceUtf8();
            else AdvanceAscii();
        }
        const std::size_t text_size = offset_ - text_begin;
        if (text_size > limits_.maximumCommentBytes)
        {
            AddDiagnostic(
                "idl.comment_too_long",
                "IDL comment exceeds the configured byte limit.",
                {begin, Position()}
            );
            return;
        }
        if (result_.comments.size() >= limits_.maximumComments)
        {
            AddDiagnostic(
                "idl.comment_limit_exceeded",
                "IDL comment count exceeds the configured limit.",
                {begin, Position()}
            );
            tokenLimitReached_ = true;
            return;
        }
        result_.comments.push_back({
            .text = std::string(source_.substr(text_begin, text_size)),
            .span = {begin, Position()},
        });
    }

    void LexPunctuation(SourcePosition begin)
    {
        const std::size_t text_begin = offset_;
        const char value = Peek();
        AdvanceAscii();
        switch (value)
        {
            case '@': AddToken(TokenKind::At, begin, text_begin); return;
            case '(': AddToken(TokenKind::LeftParen, begin, text_begin); return;
            case ')': AddToken(TokenKind::RightParen, begin, text_begin); return;
            case '{': AddToken(TokenKind::LeftBrace, begin, text_begin); return;
            case '}': AddToken(TokenKind::RightBrace, begin, text_begin); return;
            case ':': AddToken(TokenKind::Colon, begin, text_begin); return;
            case ';': AddToken(TokenKind::Semicolon, begin, text_begin); return;
            case ',': AddToken(TokenKind::Comma, begin, text_begin); return;
            default:
                AddDiagnostic(
                    "idl.unexpected_character",
                    "IDL source contains an unexpected character.",
                    {begin, Position()}
                );
                return;
        }
    }

    std::string sourceName_;
    std::string_view source_;
    ParseLimits limits_;
    LexResult result_;
    std::size_t offset_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
    bool tokenLimitReached_ = false;
};
}

std::string_view TokenKindName(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::End: return "end of input";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::Integer: return "integer";
        case TokenKind::String: return "string";
        case TokenKind::At: return "@";
        case TokenKind::LeftParen: return "(";
        case TokenKind::RightParen: return ")";
        case TokenKind::LeftBrace: return "{";
        case TokenKind::RightBrace: return "}";
        case TokenKind::Colon: return ":";
        case TokenKind::Semicolon: return ";";
        case TokenKind::Comma: return ",";
        case TokenKind::Edition: return "edition";
        case TokenKind::Schema: return "schema";
        case TokenKind::Owner: return "owner";
        case TokenKind::Import: return "import";
        case TokenKind::Model: return "model";
        case TokenKind::PrimaryId: return "primary_id";
        case TokenKind::Field: return "field";
        case TokenKind::Reserved: return "reserved";
    }
    return "token";
}

LexResult Lex(std::string_view source_name, std::string_view source, const ParseLimits& limits)
{
    return Lexer(source_name, source, limits).Run();
}
}
