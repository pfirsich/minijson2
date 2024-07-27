#pragma once

#include <cassert>
#include <concepts>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
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

    // Default constructed, just for convenience. Don't use this for anything, except assigning to
    // it later.
    Token();

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

    Parser(Parser&&) = default;
    Parser& operator=(Parser&&) = default;
    Parser(const Parser&) = default;
    Parser& operator=(const Parser&) = default;

    std::string_view input() const;
    size_t get_location(const Token& token) const;

    Token next();
    bool skip(const Token& token);

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
    const char* error_message_ = nullptr;
};

namespace structread {
    struct ParseContext {
        struct Error {
            size_t location;
            std::string message;
        };

        template <typename... Args>
        ParseContext(Args&&... args) : parser(std::forward<Args>(args)...)
        {
        }

        bool set_error(size_t location, std::string message)
        {
            error = { location, std::move(message) };
            return false;
        }

        bool set_error(const Token& token, std::string message)
        {
            return set_error(parser.get_location(token), std::move(message));
        }

        bool set_error(const Token& error_token)
        {
            assert(error_token.type() == Token::Type::Error);
            return set_error(
                error_token.error_location(), std::string(error_token.error_message()));
        }

        Parser parser;
        std::optional<Error> error;
    };

    template <typename... Args>
    std::string concat_string(Args&&... args)
    {
        std::string str;
        auto append = [&str](const auto& part) { str.append(part); };
        (append(std::forward<Args>(args)), ...);
        return str;
    }

    template <typename T>
    bool from_json(T& val, ParseContext& ctx, const Token& token, const std::string& path)
    {
        if (ctx.error) {
            return false;
        }
        if (token.type() == Token::Type::Error) {
            return ctx.set_error(token);
        }
        return from_json_impl(val, ctx, token, path);
    }

    template <typename T>
    bool from_json(T& val, ParseContext& ctx)
    {
        const auto token = ctx.parser.next();
        return from_json(val, ctx, token, "");
    }

    bool check_type(ParseContext& ctx, const Token& token, const std::string& path,
        Token::Type type, std::string_view type_name);

    bool from_json_impl(bool& v, ParseContext& ctx, const Token& token, const std::string& path);

    bool from_json_impl(
        std::string& str, ParseContext& ctx, const Token& token, const std::string& path);

    template <std::integral Target, std::integral Source>
    constexpr bool can_convert(Source val)
    {
        const auto min = std::numeric_limits<Target>::min();
        const auto max = std::numeric_limits<Target>::max();

        if constexpr (std::is_signed_v<Source> && std::is_signed_v<Target>) {
            return (min <= val) && (val <= max);
        } else if constexpr (std::is_signed_v<Source> && std::is_unsigned_v<Target>) {
            return (val >= 0) && (static_cast<std::make_unsigned_t<Source>>(val) <= max);
        } else if constexpr (std::is_unsigned_v<Source> && std::is_signed_v<Target>) {
            return val <= static_cast<std::make_unsigned_t<Target>>(max);
        } else if constexpr (std::is_unsigned_v<Source> && std::is_unsigned_v<Target>) {
            return val <= max;
        }
    }

    template <typename T>
    auto parse_integer(ParseContext& ctx, const Token& token)
    {
        if constexpr (std::is_signed_v<T>) {
            return ctx.parser.parse_int(token);
        } else {
            return ctx.parser.parse_uint(token);
        }
    }

    template <typename T>
    concept non_bool_int = std::integral<T> && !std::is_same_v<T, bool>;

    template <non_bool_int T>
    bool from_json_impl(T& v, ParseContext& ctx, const Token& token, const std::string& path)
    {
        if constexpr (std::is_signed_v<T>) {
            if (token.type() != Token::Type::Int && token.type() != Token::Type::UInt) {
                return ctx.set_error(token, concat_string(path, " must be integer"));
            };
        } else {
            if (token.type() != Token::Type::UInt) {
                return ctx.set_error(token, concat_string(path, " must be unsigned integer"));
            };
        }

        const auto raw_val = parse_integer<T>(ctx, token);
        if (!can_convert<T>(raw_val)) {
            const auto min = std::to_string(std::numeric_limits<T>::min());
            const auto max = std::to_string(std::numeric_limits<T>::max());
            return ctx.set_error(
                token, concat_string(path, " must be integer in range [", min, ", ", max, "]"));
        }
        v = static_cast<T>(raw_val);
        return true;
    }

    template <std::floating_point T>
    bool from_json_impl(T& v, ParseContext& ctx, const Token& token, const std::string& path)
    {
        if (token.type() != Token::Type::Int && token.type() != Token::Type::UInt
            && token.type() != Token::Type::Float) {
            return ctx.set_error(token, concat_string(path, " must be a number"));
        };
        v = static_cast<T>(ctx.parser.parse_float(token));
        return true;
    }

    template <typename T>
    bool from_json_impl(
        std::optional<T>& opt, ParseContext& ctx, const Token& token, const std::string& path)
    {
        return from_json(opt.emplace(), ctx, token, path);
    }

    template <typename T>
    bool from_json_impl(
        std::vector<T>& vec, ParseContext& ctx, const Token& token, const std::string& path)
    {
        if (!check_type(ctx, token, path, Token::Type::Array, "array")) {
            return false;
        }
        size_t i = 0;
        auto elem = ctx.parser.next();
        while (elem) {
            const auto val_path = concat_string(path, "[", std::to_string(i), "]");
            if (!from_json(vec.emplace_back(), ctx, elem, val_path)) {
                return false;
            }
            i++;
            elem = ctx.parser.next();
        }
        if (elem.type() == Token::Type::Error) {
            return ctx.set_error(elem);
        }
        return true;
    }

    template <typename T, size_t N>
    bool from_json_impl(
        std::array<T, N>& arr, ParseContext& ctx, const Token& token, const std::string& path)
    {
        const std::string type_name = "array of size " + std::to_string(N);
        if (!check_type(ctx, token, path, Token::Type::Array, type_name)) {
            return false;
        }
        const auto array_start = ctx.parser.get_location(token);
        size_t i = 0;
        auto elem = ctx.parser.next();
        while (elem && i < N) {
            const auto val_path = concat_string(path, "[", std::to_string(i), "]");
            if (!from_json(arr[i], ctx, elem, val_path)) {
                return false;
            }
            i++;
            elem = ctx.parser.next();
        }
        if (i != N) {
            return ctx.set_error(array_start, path + "must be " + type_name);
        }
        if (elem.type() == Token::Type::Error) {
            return ctx.set_error(elem);
        }
        return true;
    }

    template <typename T>
    struct type_meta;

    template <typename T, typename U>
    constexpr auto field(std::string_view name, U T::*ptr)
    {
        return std::tuple(name, ptr);
    }

    template <typename... Args>
    constexpr auto make_fields(Args&&... args)
    {
        return std::tuple(std::forward<Args>(args)...);
    }

    template <typename T>
    using get_type_meta = type_meta<std::decay_t<T>>;

    template <typename T>
    using type_meta_fields_type = decltype(get_type_meta<T>::fields);

    template <typename T>
    concept has_type_meta = requires { typename type_meta_fields_type<T>; };

    template <has_type_meta T, typename Func>
    void for_each_field(T& obj, Func func)
    {
        auto field_func = [&obj, &func](auto& field_desc) {
            func(std::get<0>(field_desc), obj.*std::get<1>(field_desc));
        };
        std::apply([field_func](auto&... field_desc) { (field_func(field_desc), ...); },
            get_type_meta<T>::fields);
    }

    template <typename T>
    struct optional_fields {
        static constexpr auto fields = std::make_tuple();
    };

    template <typename>
    constexpr bool is_optional_type_impl = false;

    template <typename T>
    constexpr bool is_optional_type_impl<std::optional<T>> = true;

    template <typename T>
    constexpr bool is_optional_type = is_optional_type_impl<std::remove_cvref_t<T>>;

    template <typename T, typename U>
    constexpr bool is_optional(std::string_view field_name)
    {
        if constexpr (is_optional_type<U>) {
            return true;
        }
        // char_traits<char>::compare and not strcmp, because it's constexpr
        return std::apply(
            [field_name](auto&&... opt_fields) { return ((field_name == opt_fields) || ...); },
            optional_fields<T>::fields);
    }

    template <typename Func>
    constexpr std::tuple<std::string_view, Func> key_handler(std::string_view key_name, Func func)
    {
        return { key_name, std::move(func) };
    }

    template <typename T>
    struct key_handlers {
        static constexpr auto handlers = std::make_tuple();
    };

    template <typename T>
    struct key_handler_ignore {
        bool operator()(
            std::string_view, T&, ParseContext& ctx, const Token& token, const std::string&)
        {
            return ctx.parser.skip(token);
        };
    };

    template <typename T>
    auto get_key_handler(std::string_view key_name)
    {
        std::function<bool(std::string_view key, T & obj, ParseContext & ctx, const Token& token,
            const std::string& path)>
            handler;
        auto apply_handler = [key_name, &handler](auto& key_handler) -> bool {
            const auto name = std::get<0>(key_handler);
            auto& func = std::get<1>(key_handler);
            if (key_name == name) {
                handler = func;
                return true;
            }
            return false;
        };

        std::apply([&](auto&&... handlers) { return (apply_handler(handlers) || ...); },
            key_handlers<T>::handlers);
        return handler;
    }

    template <has_type_meta T>
    bool from_json_impl(T& obj, ParseContext& ctx, const Token& token, const std::string& path)
    {
        if (!check_type(ctx, token, path, Token::Type::Object, "object")) {
            return false;
        }
        const auto obj_location = ctx.parser.get_location(token);

        bool known_key = false;
        std::string_view key_str;
        // Maybe replace this map with an array in the future (has some complications)
        std::unordered_map<std::string_view, bool> keys_found;

        auto apply_field = [&](auto& field) {
            const auto field_name = std::get<0>(field);
            auto& obj_field = obj.*std::get<1>(field);
            if (!is_optional<T, decltype(obj_field)>(field_name)) {
                keys_found.emplace(field_name, false);
            }
            if (key_str == field_name) {
                known_key = true;
                keys_found[field_name] = true;
                const auto field_path = concat_string(path, ".", field_name);
                if (!from_json(obj_field, ctx, ctx.parser.next(), field_path)) {
                    return false;
                }
            }
            return true;
        };

        auto key = ctx.parser.next();
        while (key) {
            key_str = ctx.parser.parse_string(key);
            known_key = false;

            auto handler = get_key_handler<T>(key_str);
            if (handler) {
                known_key = true;
                if (!handler(key_str, obj, ctx, ctx.parser.next(), path)) {
                    return false;
                }
            } else if (!std::apply(
                           [&](auto&... fields_args) { return (apply_field(fields_args) && ...); },
                           get_type_meta<T>::fields)) {
                return false;
            }

            if (!known_key) {
                return ctx.set_error(key, concat_string(path, ": Unknown key '", key_str, "'"));
            }

            key = ctx.parser.next();
        }

        if (key.type() == Token::Type::Error) {
            return ctx.set_error(key);
        }

        for (const auto& [key, found] : keys_found) {
            if (!found) {
                return ctx.set_error(
                    obj_location, concat_string(path, ": Missing key '", key, "'"));
            }
        }
        return true;
    }
}
}

// These macros suck so fucking much, holy shit. PLEASE JUST GIVE US REFLECTION, IT'S BEEN 10 YEARS
// NOW (N3996).

// Without these macros you would have to do this tedious garbage:
/*
        template <>
        struct type_meta<Asset> {
            static constexpr auto fields = make_fields(
                field("generator", &Asset::generator),
                field("version", &Asset::version)
            );
        };
*/

#define MJ2_VA_ARGS_COUNT(...)                                                                     \
    MJ2_VA_ARGS_COUNT_IMPL(                                                                        \
        __VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
// Essentially you return the 21st argument passed to this macro. By passing varargs before that you
// can shift towards a higher number, effectively counting arguments.
#define MJ2_VA_ARGS_COUNT_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15,   \
    _16, _17, _18, _19, _20, N, ...)                                                               \
    N

#define MJ2_EXPAND_ARGS(macro, type, ...)                                                          \
    MJ2_EXPAND_ARGS_IMPL(MJ2_VA_ARGS_COUNT(__VA_ARGS__), macro, type, __VA_ARGS__)

// We have an extra level of indirection here to force MJ2_VA_ARGS_COUNT(...) to be expanded,
// otherwise we get MJ2_EXPAND_ARGS_HELPER_MJ2_VA_ARGS_COUNT(...)(...). I have read about it a
// couple of times and still don't really get it, but I feel like I am making myself dumber by
// understanding it, so I don't care.
#define MJ2_EXPAND_ARGS_IMPL(N, macro, type, ...) MJ2_EXPAND_ARGS_IMPL_(N, macro, type, __VA_ARGS__)

#define MJ2_EXPAND_ARGS_IMPL_(N, macro, type, ...)                                                 \
    MJ2_EXPAND_ARGS_HELPER_##N(macro, type, __VA_ARGS__)

#define MJ2_EXPAND_ARGS_HELPER_0(macro, type, ...)
#define MJ2_EXPAND_ARGS_HELPER_1(macro, type, arg, ...) macro(type, arg)
#define MJ2_EXPAND_ARGS_HELPER_2(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_1(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_3(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_2(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_4(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_3(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_5(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_4(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_6(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_5(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_7(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_6(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_8(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_7(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_9(macro, type, arg, ...)                                            \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_8(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_10(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_9(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_11(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_10(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_12(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_11(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_13(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_12(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_14(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_13(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_15(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_14(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_16(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_15(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_17(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_16(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_18(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_17(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_19(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_18(macro, type, __VA_ARGS__)
#define MJ2_EXPAND_ARGS_HELPER_20(macro, type, arg, ...)                                           \
    macro(type, arg), MJ2_EXPAND_ARGS_HELPER_19(macro, type, __VA_ARGS__)

#define MJ2_FIELD(type, field_name) minijson2::structread::field(#field_name, &type::field_name)
#define MJ2_FIELDS(type, ...) MJ2_EXPAND_ARGS(MJ2_FIELD, type, __VA_ARGS__)

#define MJ2_TYPE_META(TYPE_, ...)                                                                  \
    template <>                                                                                    \
    struct minijson2::structread::type_meta<TYPE_> {                                               \
        static constexpr auto fields                                                               \
            = minijson2::structread::make_fields(MJ2_FIELDS(TYPE_, __VA_ARGS__));                  \
    };

#define MJ2_OPTIONAL_FIELD(type, field_name) #field_name
#define MJ2_OPTIONAL_FIELDS_(type, ...) MJ2_EXPAND_ARGS(MJ2_OPTIONAL_FIELD, type, __VA_ARGS__)

#define MJ2_OPTIONAL_FIELDS(TYPE_, ...)                                                            \
    template <>                                                                                    \
    struct minijson2::structread::optional_fields<TYPE_> {                                         \
        static constexpr auto fields = std::make_tuple(MJ2_OPTIONAL_FIELDS_(TYPE_, __VA_ARGS__));  \
    };
