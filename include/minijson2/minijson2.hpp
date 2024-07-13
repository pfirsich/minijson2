#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

namespace minijson2 {

// Escape in place
size_t escape_string(char* str, size_t len);

std::string escape_string(std::string_view str);

struct Context {
    size_t line_number;
    size_t column;
    std::string_view line;
};

Context get_context(std::string_view str, size_t cursor);

struct Token {
    // End on eof or if array/object ends
    enum class Type : uint8_t {
        Null,
        Bool,
        UInt,
        Int,
        Float,
        String,
        Array,
        Object,

        EndArray,
        EndObject,
        Eof,
        Error,
    };

    // Regular token
    Token(Type type, std::string_view str);

    // Error token
    Token(size_t cursor, const char* message);

    Type type() const;

    // Only for non-error tokens
    std::string_view string() const;

    // Only for error tokens
    size_t error_location() const;
    std::string_view error_message() const;

    // Whether to proceed iteration (type is not EndArray, EndObject, Eof or Error)
    explicit operator bool() const;

private:
    // For most tokens I just want to store a type and a view on the string of that token.
    // For errors I want to store the error type and a string to a null-teminated static string.
    // I use a u32 for the length, so the whole structure fits into two words on 64 bit machines
    // (like a string view). Of course this limits the length of tokens to 4GB (fine for my use
    // cases). I tried storing both the length and the type in a single u64 (cut off a byte for the
    // type), but the bitmasking and stuff is too annoying and too complicated for a simple library
    // like this. Somehow I measured that bitmasking to be slightly faster than the current method,
    // but that makes no sense to me.
    const char* str_ = 0;
    uint32_t length_ = 0;
    Type type_;
};

class Parser {
public:
    // Mutable reference to escape strings in-place
    Parser(std::string& input);

    std::string_view input() const;

    Token next();

    // Don't call this function twice for the same token, as it might escape the same string twice
    // (which would be wrong).
    // Also keep in mind that the Token string_view will not be correct afterwards.
    std::string_view parse_string(const Token& token, bool escape_in_place = true);
    int64_t parse_int(const Token& token);
    uint64_t parse_uint(const Token& token);
    double parse_float(const Token& token);
    bool parse_bool(const Token& token);

private:
    enum class ExpectNext : uint8_t {
        Error,
        Value,
        ObjectKey,
        ObjectValue,
        ArrayValue,
    };

    Token on_value();
    Token on_object_key();
    Token on_object_value();
    Token on_array_value();

    void skip_whitespace();

    Token string_token();
    Token number_token(std::string_view value);
    Token error_token(const char* message);

    std::string& input_buffer_;
    std::string_view input_;
    size_t cursor_ = 0;
    std::vector<ExpectNext> expect_next_;
};

}