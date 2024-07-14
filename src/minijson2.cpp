#include "minijson2/minijson2.hpp"

#include <cassert>
#include <charconv>
#include <optional>

namespace {
bool is_whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\t';
}

template <typename T>
std::optional<T> parse_number(std::string_view str)
{
    T value {};
    const auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc() || ptr != str.data() + str.size()) {
        return std::nullopt;
    }
    return value;
}

uint16_t parse_unicode_escape_hex(std::string_view str)
{
    uint16_t value = 0;
    [[maybe_unused]] const auto [ptr, ec]
        = std::from_chars(str.data(), str.data() + str.size(), value, 16);
    assert(ec == std::errc() && ptr == str.data() + str.size());
    return value;
}

size_t encode_utf8(char* dst, uint16_t cp)
{
    if (cp <= 0x7F) {
        // 1-byte sequence
        dst[0] = static_cast<char>(cp);
        return 1;
    } else if (cp <= 0x7FF) {
        // 2-byte sequence
        dst[0] = static_cast<char>(0b11000000 | ((cp >> 6) & 0b00011111));
        dst[1] = static_cast<char>(0b10000000 | (cp & 0b00111111));
        return 2;
    }

    // 3-byte sequence
    dst[0] = static_cast<char>(0b11100000 | ((cp >> 12) & 0b00001111));
    dst[1] = static_cast<char>(0b10000000 | ((cp >> 6) & 0b00111111));
    dst[2] = static_cast<char>(0b10000000 | (cp & 0b00111111));
    return 3;
}
}

namespace minijson2 {

size_t escape_string(char* str, size_t len)
{
    size_t dst = 0;
    for (size_t src = 0; src < len; ++src) {
        if (str[src] == '\\') {
            assert(src + 1 < len);
            src++;
            switch (str[src]) {
            case '"':
                str[dst++] = '"';
                break;
            case '\\':
                str[dst++] = '\\';
                break;
            case '/':
                str[dst++] = '/';
                break;
            case 'b':
                str[dst++] = '\b';
                break;
            case 'f':
                str[dst++] = '\f';
                break;
            case 'n':
                str[dst++] = '\n';
                break;
            case 'r':
                str[dst++] = '\r';
                break;
            case 't':
                str[dst++] = '\t';
                break;
            case 'u': {
                // TODO: Surrogate pairs to encode code points above U+FFFF.
                assert(src + 4 < len);
                const std::string_view seq(str + src + 1, 4);
                const auto val = parse_unicode_escape_hex(seq);
                dst += encode_utf8(str + dst, val);
                src += 4; // the last hex char will be skipped by the for loop
                break;
            }
            default:
                break;
            }
        } else {
            str[dst++] = str[src];
        }
    }
    // Fill up with zeros to make moved chars more obvious
    for (size_t i = dst; i < len; ++i) {
        str[i] = '\0';
    }
    return dst;
}

std::string escape_string(std::string_view str)
{
    std::string ret(str);
    ret.resize(escape_string(ret.data(), ret.size()));
    return ret;
}

Context get_context(std::string_view str, size_t cursor)
{
    size_t line_start = 0;
    size_t line_number = 1;
    size_t line_end = str.size();
    for (size_t i = 0; i < str.size(); ++i) {
        const auto nl = str.find('\n', i);
        if (nl > cursor) {
            line_end = nl;
            break;
        }
        line_start = nl + 1;
        line_number++;
        i = nl;
    }
    return Context {
        .line_number = line_number,
        .column = cursor - line_start,
        .line = str.substr(line_start, line_end - line_start),
    };
}

// Regular token
Token::Token(Type type, std::string_view str) : str_(str.data()), length_(str.size()), type_(type)
{
}

Token::Token(size_t cursor, const char* message)
    : str_(message)
    , length_(cursor)
    , type_(Type::Error)
{
}

Token::Type Token::type() const
{
    return type_;
}

std::string_view Token::string() const
{
    assert(type_ != Type::Error);
    return std::string_view(str_, length_);
}

size_t Token::error_location() const
{
    assert(type_ == Type::Error);
    return length_;
}

std::string_view Token::error_message() const
{
    assert(type_ == Type::Error);
    return str_;
}

Token::operator bool() const
{
    return static_cast<uint8_t>(type_) < static_cast<uint8_t>(Type::EndArray);
}

Parser::Parser(std::string& input) : input_buffer_(input), input_(input)
{
    expect_next_.reserve(512);
    // Only one value per document
    expect_next_.push_back(ExpectNext::Value);
}

size_t Parser::get_location(const Token& token) const
{
    return token.string().data() - input_buffer_.data();
}

std::string_view Parser::input() const
{
    return input_;
}

Token Parser::next()
{
    if (expect_next_.empty()) {
        return Token(Token::Type::Eof, input_.substr(cursor_, input_.size() - cursor_));
    }
    const auto expect = expect_next_.back();
    expect_next_.pop_back();

    switch (expect) {
    case ExpectNext::Error:
        return error_token("Abort after previous error");
    case ExpectNext::Value:
        return on_value();
    case ExpectNext::ObjectKey:
        return on_object_key();
    case ExpectNext::ObjectValue:
        return on_object_value();
    case ExpectNext::ArrayValue:
        return on_array_value();
    default:
        std::abort();
    }
}

std::string_view Parser::parse_string(const Token& token, bool escape_in_place)
{
    assert(token.type() == Token::Type::String);
    const auto sv = token.string();
    auto len = sv.size();
    if (escape_in_place) {
        const auto offset = sv.data() - input_buffer_.data();
        len = escape_string(input_buffer_.data() + offset, sv.size());
    }
    return sv.substr(0, len);
}

int64_t Parser::parse_int(const Token& token)
{
    assert(token.type() == Token::Type::UInt || token.type() == Token::Type::Int);
    // should work if type is uint or int
    return parse_number<int64_t>(token.string()).value();
}

uint64_t Parser::parse_uint(const Token& token)
{
    assert(token.type() == Token::Type::UInt);
    // should work if type is uint
    return parse_number<uint64_t>(token.string()).value();
}

double Parser::parse_float(const Token& token)
{
    assert(token.type() == Token::Type::UInt || token.type() == Token::Type::Int
        || token.type() == Token::Type::Float);
    return parse_number<double>(token.string()).value();
}

bool Parser::parse_bool(const Token& token)
{
    assert(token.string() == "true" || token.string() == "false");
    return token.string() == "true";
}

Token Parser::on_value()
{
    skip_whitespace();
    if (cursor_ >= input_.size()) {
        return error_token("Expected value");
    }

    if (input_[cursor_] == '"') {
        return string_token();
    }

    if (input_[cursor_] == '{') {
        cursor_++; // skip opening brace
        expect_next_.push_back(ExpectNext::ObjectKey);
        return Token(Token::Type::Object, input_.substr(cursor_ - 1, 1));
    }

    if (input_[cursor_] == '[') {
        cursor_++; // skip opening bracket
        expect_next_.push_back(ExpectNext::ArrayValue);
        return Token(Token::Type::Array, input_.substr(cursor_ - 1, 1));
    }

    // true, false, null, number (or error)
    const auto value_chars = "0123456789abcdefghijlmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.+-";
    auto value_end = input_.find_first_not_of(value_chars, cursor_);
    if (value_end == std::string::npos) {
        value_end = input_.size();
    }

    const auto value = input_.substr(cursor_, value_end - cursor_);
    if (value.empty()) {
        return error_token("Value must not be empty");
    }

    if (value == "null") {
        cursor_ += value.size(); // skip value
        return Token(Token::Type::Null, value);
    }

    if (value == "true" || value == "false") {
        cursor_ += value.size(); // skip value
        return Token(Token::Type::Bool, value);
    }

    const auto number_chars = "0123456789eE.-+";
    const auto non_num_pos = value.find_first_not_of(number_chars);
    if (non_num_pos != std::string_view::npos) {
        cursor_ += non_num_pos; // skip to first non-number char
        return error_token("Expected string, array, object, null, boolean or number");
    }

    return number_token(value);
}

Token Parser::on_object_key()
{
    skip_whitespace();
    if (cursor_ >= input_.size()) {
        return error_token("Unterminated object");
    }
    if (input_[cursor_] == '}') {
        cursor_++; // skip closing brace
        return Token(Token::Type::EndObject, input_.substr(cursor_ - 1, 1));
    }
    if (input_[cursor_] == ',') {
        cursor_++; // skip comma
    }

    expect_next_.push_back(ExpectNext::ObjectValue);

    return string_token();
}

Token Parser::on_object_value()
{
    skip_whitespace();
    if (cursor_ >= input_.size()) {
        return error_token("Unterminated object");
    }
    if (input_[cursor_] != ':') {
        return error_token("Expected ':' after object key");
    }
    cursor_++; // skip colon

    expect_next_.push_back(ExpectNext::ObjectKey);
    return on_value();
}

Token Parser::on_array_value()
{
    // This technically allows leading commas, but I can't come up with an elegant fix
    skip_whitespace();
    if (cursor_ >= input_.size()) {
        return error_token("Unterminated array");
    }
    if (input_[cursor_] == ']') {
        cursor_++; // skip closing bracket
        return Token(Token::Type::EndArray, input_.substr(cursor_ - 1, 1));
    }
    if (input_[cursor_] == ',') {
        cursor_++; // skip comma
    }

    // After an array value, we expect another array value
    expect_next_.push_back(ExpectNext::ArrayValue);

    return on_value();
}

void Parser::skip_whitespace()
{
    while (cursor_ < input_.size() && is_whitespace(input_[cursor_])) {
        cursor_++;
    }
}

Token Parser::string_token()
{
    skip_whitespace();
    assert(cursor_ < input_.size());
    assert(input_[cursor_] == '"');
    cursor_++;
    const auto start = cursor_;

    auto end = std::string_view::npos;
    for (size_t i = cursor_; i < input_.size(); ++i) {
        if (input_[i] == '"' && input_[i - 1] != '\\') {
            end = i;
            break;
        }
    }

    if (end == std::string_view::npos) {
        cursor_--; // Point to starting double quote
        return error_token("Unterminated string");
    }

    while (cursor_ < end) {
        if (input_[cursor_] == '\\') {
            if (cursor_ + 1 >= input_.size()) {
                return error_token("Incomplete escape sequence");
            }
            cursor_++; // skip backslash

            const auto c = input_[cursor_];
            if (c == 'u') {
                cursor_++; // skip 'u'
                if (cursor_ + 4 >= input_.size()) {
                    return error_token("Incomplete unicode escape sequence");
                }
                const auto seq = std::string_view(input_.substr(cursor_, 4));
                const auto non_hex = seq.find_first_not_of("0123456789abcdefABCDEF");
                if (non_hex != std::string_view::npos) {
                    cursor_ += non_hex;
                    return error_token("Incomplete unicode escape sequence");
                }
                cursor_ += 4; // skip 4 hex chars
            } else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n'
                || c == 'r' || c == 't') {
                cursor_++; // skip escape sequence
            } else {
                return error_token("Invalid escape sequence");
            }
        } else {
            assert(input_[cursor_] != '"');
            cursor_++;
        }
    }
    cursor_++; // skip ending double quote
    return Token(Token::Type::String, input_.substr(start, end - start));
}

Token Parser::number_token(std::string_view value)
{
    assert(!value.empty());
    cursor_ += value.size(); // skip value

    const auto int_chars = "0123456789-";
    if (value.find_first_not_of(int_chars) != std::string_view::npos) {
        return Token(Token::Type::Float, value);
    }
    if (value[0] == '-') {
        return Token(Token::Type::Int, value);
    }
    return Token(Token::Type::UInt, value);
}

Token Parser::error_token(const char* message)
{
    // When calling error_token(), cursor_ should always point to the error
    expect_next_.push_back(ExpectNext::Error); // return errors forever
    return Token(cursor_, message);
}
}
