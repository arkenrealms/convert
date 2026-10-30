#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "arken_schema_idl_v1/idl.hpp"

namespace arken::schema::idl::v1::detail
{
enum class TokenKind
{
    End,
    Identifier,
    Integer,
    String,
    At,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Colon,
    Semicolon,
    Comma,
    Edition,
    Schema,
    Owner,
    Import,
    Model,
    PrimaryId,
    Field,
    Reserved,
};

struct Token
{
    TokenKind kind = TokenKind::End;
    std::string text;
    std::string decodedText;
    SourceSpan span;
};

struct LexResult
{
    std::vector<Token> tokens;
    std::vector<Comment> comments;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] std::string_view TokenKindName(TokenKind kind);
[[nodiscard]] LexResult Lex(
    std::string_view source_name,
    std::string_view source,
    const ParseLimits& limits
);
}
