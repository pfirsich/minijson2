#include <concepts>
#include <iostream>
#include <limits>
#include <optional>
#include <unordered_map>

#include <minijson2/minijson2.hpp>

using namespace minijson2;

////////////////////////////////////////////////////////////

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
        return set_error(error_token.error_location(), std::string(error_token.error_message()));
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

bool check_type(ParseContext& ctx, const Token& token, const std::string& path, Token::Type type,
    std::string_view type_name)
{
    if (token.type() != type) {
        return ctx.set_error(token, concat_string(path, " must be ", type_name));
    }
    return true;
}

bool from_json_impl(bool& v, ParseContext& ctx, const Token& token, const std::string& path)
{
    if (!check_type(ctx, token, path, Token::Type::Bool, "boolean")) {
        return false;
    }
    v = ctx.parser.parse_bool(token);
    return true;
}

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

bool from_json_impl(
    std::string& str, ParseContext& ctx, const Token& token, const std::string& path)
{
    if (!check_type(ctx, token, path, Token::Type::String, "string")) {
        return false;
    }
    str.assign(ctx.parser.parse_string(token));
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

template <typename T>
struct type_meta;

template <typename T, typename F>
constexpr auto field(const char* name, F T::*f)
{
    return std::tuple(name, f);
}

template <typename... Args>
constexpr auto make_fields(Args&&... args)
{
    return std::tuple(std::forward<Args>(args)...);
}

template <typename>
constexpr bool is_optional_impl = false;

template <typename T>
constexpr bool is_optional_impl<std::optional<T>> = true;

template <typename T>
constexpr bool is_optional = is_optional_impl<std::remove_cvref_t<T>>;

template <typename T>
bool from_json_impl(T& obj, ParseContext& ctx, const Token& token, const std::string& path)
{
    if (!check_type(ctx, token, path, Token::Type::Object, "object")) {
        return false;
    }
    const auto obj_location = ctx.parser.get_location(token);

    bool known_key = false;
    std::string_view key_str;
    std::unordered_map<std::string, bool> keys_found;

    const auto apply_field = [&](auto& field) {
        const auto field_name = std::get<0>(field);
        auto& obj_field = obj.*std::get<1>(field);
        if constexpr (!is_optional<decltype(obj_field)>) {
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

    const auto& fields = type_meta<T>::fields;
    auto key = ctx.parser.next();
    while (key) {
        key_str = ctx.parser.parse_string(key);
        known_key = false;

        if (!std::apply(
                [&](auto&... fields_args) { return (apply_field(fields_args) && ...); }, fields)) {
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
            return ctx.set_error(obj_location, concat_string(path, ": Missing key '", key, "'"));
        }
    }
    return true;
}

// I know macros are evil, but until we have reflection there is no way to make this ergonomic and
// robust, but to use macros.
#define COUNT_ARGS(...) COUNT_ARGS_(__VA_ARGS__, RSEQ_N())
#define COUNT_ARGS_(...) ARG_N(__VA_ARGS__)
#define ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18,     \
    _19, _20, N, ...)                                                                              \
    N
#define RSEQ_N() 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define CONCATENATE(arg1, arg2) CONCATENATE1(arg1, arg2)
#define CONCATENATE1(arg1, arg2) CONCATENATE2(arg1, arg2)
#define CONCATENATE2(arg1, arg2) arg1##arg2

// Utility macros to generate the fields
#define FIELD_PAIR_1(type, arg) field(#arg, &type::arg)
#define FIELD_PAIR_2(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_1(type, __VA_ARGS__)
#define FIELD_PAIR_3(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_2(type, __VA_ARGS__)
#define FIELD_PAIR_4(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_3(type, __VA_ARGS__)
#define FIELD_PAIR_5(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_4(type, __VA_ARGS__)
#define FIELD_PAIR_6(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_5(type, __VA_ARGS__)
#define FIELD_PAIR_7(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_6(type, __VA_ARGS__)
#define FIELD_PAIR_8(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_7(type, __VA_ARGS__)
#define FIELD_PAIR_9(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_8(type, __VA_ARGS__)
#define FIELD_PAIR_10(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_9(type, __VA_ARGS__)
#define FIELD_PAIR_11(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_10(type, __VA_ARGS__)
#define FIELD_PAIR_12(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_11(type, __VA_ARGS__)
#define FIELD_PAIR_13(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_12(type, __VA_ARGS__)
#define FIELD_PAIR_14(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_13(type, __VA_ARGS__)
#define FIELD_PAIR_15(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_14(type, __VA_ARGS__)
#define FIELD_PAIR_16(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_15(type, __VA_ARGS__)
#define FIELD_PAIR_17(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_16(type, __VA_ARGS__)
#define FIELD_PAIR_18(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_17(type, __VA_ARGS__)
#define FIELD_PAIR_19(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_18(type, __VA_ARGS__)
#define FIELD_PAIR_20(type, arg, ...) FIELD_PAIR_1(type, arg), FIELD_PAIR_19(type, __VA_ARGS__)

// Select the appropriate FIELD_PAIR_X macro based on the number of arguments
#define FIELD_PAIRS(type, ...) CONCATENATE(FIELD_PAIR_, COUNT_ARGS(__VA_ARGS__))(type, __VA_ARGS__)

#define TYPE_META(type, ...)                                                                       \
    template <>                                                                                    \
    struct type_meta<type> {                                                                       \
        static constexpr auto fields = make_fields(FIELD_PAIRS(type, __VA_ARGS__));                \
    };

////////////////////////////////////////////////////////////

struct Asset {
    std::string generator;
    std::string version;
    uint16_t num_version;
};
TYPE_META(Asset, generator, version, num_version)

// The macro is much nicer than this:
/*template <>
struct type_meta<Asset> {
    // clang-format off
    static constexpr auto fields = make_fields(
        field("generator", &Asset::generator),
        field("version", &Asset::version)
    );
    // clang-format on
};*/

struct Scene {
    std::string name;
    float weight;
    std::vector<size_t> nodes;
    std::optional<size_t> camera;
};
TYPE_META(Scene, name, weight, nodes, camera)

struct Gltf {
    Asset asset;
    std::vector<Scene> scenes;
};
TYPE_META(Gltf, asset, scenes)

int main()
{
    std::string input(R"(
        {
            "asset": {
                "generator": "joel",
                "version": "6.9",
                "num_version": 15
            },
            "scenes": [
                {
                    "name": "scene A",
                    "weight": 1, 
                    "nodes": [0, 1, 2, 3, 4]
                },
                {
                    "name": "scene B",
                    "weight": 1.5,
                    "nodes": [5, 6, 7, 8],
                    "camera": 5
                }
            ]
        }
    )");

    ParseContext ctx(input);
    Gltf gltf;
    if (!from_json(gltf, ctx)) {
        std::cerr << "Error: " << ctx.error.value().message << std::endl;
        const auto err_ctx = get_context(ctx.parser.input(), ctx.error.value().location);
        std::cerr << "Line " << err_ctx.line_number << std::endl;
        std::cerr << err_ctx.line << std::endl;
        std::cerr << std::string(err_ctx.column, ' ') << "^" << std::endl;
        return 1;
    }

    std::cout << "asset.generator: " << gltf.asset.generator << std::endl;
    std::cout << "asset.version: " << gltf.asset.version << std::endl;
    std::cout << "asset.num_version: " << gltf.asset.num_version << std::endl;
    for (size_t s = 0; s < gltf.scenes.size(); ++s) {
        std::cout << "scenes[" << s << "].name: " << gltf.scenes[s].name << std::endl;
        std::cout << "scenes[" << s << "].weight: " << gltf.scenes[s].weight << std::endl;
        if (gltf.scenes[s].camera) {
            std::cout << "scenes[" << s << "].camera: " << *gltf.scenes[s].camera << std::endl;
        }
        for (size_t n = 0; n < gltf.scenes[s].nodes.size(); ++n) {
            std::cout << "scenes[" << s << "].nodes[" << n << "]: " << gltf.scenes[s].nodes[n]
                      << std::endl;
        }
    }
}