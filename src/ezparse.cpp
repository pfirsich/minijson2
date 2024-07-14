#include <iostream>
#include <optional>
#include <unordered_map>

#include <minijson2/minijson2.hpp>

using namespace minijson2;

////////////////////////////////////////////////////////////

// todo: add parser to context?
// todo: general getters for integers and floats that check range

struct ParseContext {
    struct Error {
        size_t location;
        std::string message;
    };

    bool check(const Parser& parser, const Token& token, Token::Type type,
        std::string_view type_name, std::string_view path)
    {
        if (error) {
            return false;
        }
        if (token.type() == Token::Type::Error) {
            error = Error { token.error_location(), std::string(token.error_message()) };
            return false;
        }
        if (token.type() != type) {
            auto message = std::string(path);
            message.append(" must be ");
            message.append(type_name);
            error = Error { parser.get_location(token), std::move(message) };
        }
        return true;
    }

    std::optional<Error> error;
};

bool from_json(bool& v, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    if (!ctx.check(parser, token, Token::Type::Bool, "boolean", path)) {
        return false;
    }
    v = parser.parse_bool(token);
    return true;
}

// TODO: This should check for Int as well!
bool from_json(uint64_t& v, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    if (!ctx.check(parser, token, Token::Type::UInt, "unsigned integer", path)) {
        return false;
    }
    v = parser.parse_uint(token);
    return true;
}

bool from_json(int64_t& v, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    if (!ctx.check(parser, token, Token::Type::Int, "integer", path)) {
        return false;
    }
    v = parser.parse_uint(token);
    return true;
}

// TODO: This should actually check for Int and UInt too!
bool from_json(double& v, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    if (!ctx.check(parser, token, Token::Type::Float, "unsigned integer", path)) {
        return false;
    }
    v = parser.parse_uint(token);
    return true;
}

bool from_json(std::string& str, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    if (!ctx.check(parser, token, Token::Type::String, "string", path)) {
        return false;
    }
    str.assign(parser.parse_string(token));
    return true;
}

template <typename T>
bool from_json(
    std::optional<T>& opt, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    return from_json(opt.emplace(), parser, token, ctx, path);
}

template <typename T>
bool from_json(
    std::vector<T>& vec, Parser& parser, Token token, ParseContext& ctx, std::string path)
{
    if (!ctx.check(parser, token, Token::Type::Array, "array", path)) {
        return false;
    }
    size_t i = 0;
    auto elem = parser.next();
    while (elem) {
        const auto val_path = path + "[" + std::to_string(i) + "]";
        if (!from_json(vec.emplace_back(), parser, elem, ctx, val_path)) {
            return false;
        }
        i++;
        elem = parser.next();
    }
    if (elem.type() == Token::Type::Error) {
        ctx.error
            = ParseContext::Error { elem.error_location(), std::string(elem.error_message()) };
        return false;
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
bool from_json(T& obj, Parser& parser, Token token, ParseContext& ctx, std::string path = "")
{
    if (!ctx.check(parser, token, Token::Type::Object, "object", path)) {
        return false;
    }
    const auto obj_location = parser.get_location(token);

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
            if (!from_json(obj_field, parser, parser.next(), ctx, path + "." + field_name)) {
                return false;
            }
        }
        return true;
    };

    const auto& fields = type_meta<T>::fields;
    auto key = parser.next();
    while (key) {
        key_str = parser.parse_string(key);
        known_key = false;

        if (!std::apply(
                [&](auto&... fields_args) { return (apply_field(fields_args) && ...); }, fields)) {
            return false;
        }

        if (!known_key) {
            ctx.error = ParseContext::Error { parser.get_location(key),
                path + ": Unknown key '" + std::string(key_str) + "'" };
            return false;
        }

        key = parser.next();
    }

    if (key.type() == Token::Type::Error) {
        ctx.error = ParseContext::Error { key.error_location(), std::string(key.error_message()) };
        return false;
    }

    for (const auto& [key, found] : keys_found) {
        if (!found) {
            ctx.error = ParseContext::Error { obj_location, path + ": Missing key '" + key + "'" };
            return false;
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
};
TYPE_META(Asset, generator, version)

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
    std::vector<size_t> nodes;
    std::optional<size_t> camera;
};
TYPE_META(Scene, name, nodes, camera)

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
                "version": "6.9"
            },
            "scenes": [
                {
                    "name": "scene A",
                    "nodes": [0, 1, 2, 3, 4]
                },
                {
                    "name": "scene B",
                    "nodes": [5, 6, 7, 8],
                    "camera": 5
                }
            ]
        }
    )");

    Parser parser(input);
    ParseContext ctx;

    Gltf gltf;
    if (!from_json(gltf, parser, parser.next(), ctx)) {
        std::cerr << "Error: " << ctx.error.value().message << std::endl;
        const auto err_ctx = get_context(parser.input(), ctx.error.value().location);
        std::cerr << "Line " << err_ctx.line_number << std::endl;
        std::cerr << err_ctx.line << std::endl;
        std::cerr << std::string(err_ctx.column, ' ') << "^" << std::endl;
        return 1;
    }

    std::cout << "asset.generator: " << gltf.asset.generator << std::endl;
    std::cout << "asset.version: " << gltf.asset.version << std::endl;
    for (size_t s = 0; s < gltf.scenes.size(); ++s) {
        std::cout << "scenes[" << s << "].name: " << gltf.scenes[s].name << std::endl;
        if (gltf.scenes[s].camera) {
            std::cout << "scenes[" << s << "].camera: " << *gltf.scenes[s].camera << std::endl;
        }
        for (size_t n = 0; n < gltf.scenes[s].nodes.size(); ++n) {
            std::cout << "scenes[" << s << "].nodes[" << n << "]: " << gltf.scenes[s].nodes[n]
                      << std::endl;
        }
    }
}