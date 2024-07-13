#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>

#include <map>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <variant>

#include <minijson2/minijson2.hpp>

using namespace minijson2;

// Taken from minijson (v1)
class JsonValue {
public:
    struct Invalid { };
    struct Null { };
    using Bool = bool;
    using Number = double;
    using String = std::pmr::string;
    using Array = std::pmr::vector<JsonValue>;
    // map instead of unordered_map, because it supports incomplete value types
    // We need a transparent comparator to enable .find with std::string_view
    using Object = std::pmr::map<std::pmr::string, JsonValue, std::less<>>;

    enum class Type {
        Invalid = 0,
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    JsonValue() : value_(Invalid {}) { }
    JsonValue(Null n) : value_(std::move(n)) { } // better move that empty struct! #highperformance
    JsonValue(bool b) : value_(b) { }
    JsonValue(double v) : value_(v) { }
    JsonValue(String s) : value_(std::move(s)) { }
    JsonValue(Array v) : value_(std::move(v)) { }
    JsonValue(Object m) : value_(std::move(m)) { }

    // This is a bit brittle, but if we are being honest, doing a switch is not super robust either.
    // I have messed that up before too.
    Type type() const { return static_cast<Type>(value_.index()); }

    template <typename T>
    bool is() const
    {
        return std::holds_alternative<T>(value_);
    }

    bool isValid() const { return !is<Invalid>(); }
    bool isNull() const { return is<Null>(); }
    bool isBool() { return is<Bool>(); }
    bool isNumber() const { return is<Number>(); }
    bool isString() const { return is<String>(); }
    bool isArray() const { return is<Array>(); }
    bool isObject() const { return is<Object>(); }

    // The following "as" and "to" methods are weirdly named.
    // I don't know how to call them. I just want it to be ergonomic (short!) and this is what I
    // picked.

    template <typename T>
    const T& as() const
    {
        return std::get<T>(value_);
    }

    const Bool& asBool() const { return as<Bool>(); }
    const Number& asNumber() const { return as<Number>(); }
    const String& asString() const { return as<String>(); }
    const Array& asArray() const { return as<Array>(); }
    const Object& asObject() const { return as<Object>(); }

    template <typename T>
    const T* to() const
    {
        if (is<T>()) {
            return &as<T>();
        }
        return nullptr;
    }

    const Bool* toBool() const { return to<Bool>(); }
    const Number* toNumber() const { return to<Number>(); }
    const String* toString() const { return to<String>(); }
    const Array* toArray() const { return to<Array>(); }
    const Object* toObject() const { return to<Object>(); }

    size_t size() const
    {
        if (!isValid() || isNull()) {
            return 0;
        } else if (isArray()) {
            return std::get<Array>(value_).size();
        } else if (isObject()) {
            return std::get<Object>(value_).size();
        } else {
            return 1;
        }
    }

private:
    std::variant<Invalid, Null, Bool, Number, String, Array, Object> value_;
};

JsonValue to_dom(Parser& parser, const Token& token, std::pmr::memory_resource* mem_res)
{
    switch (token.type()) {
    case Token::Type::Null:
        return JsonValue(JsonValue::Null {});
    case Token::Type::String:
        return JsonValue(JsonValue::String(parser.parse_string(token), mem_res));
    case Token::Type::Int:
    case Token::Type::UInt:
    case Token::Type::Float:
        return JsonValue(parser.parse_float(token));
    case Token::Type::Bool:
        return JsonValue(parser.parse_bool(token));
    case Token::Type::Array: {
        JsonValue::Array array(mem_res);
        while (const auto elem = parser.next()) {
            array.push_back(to_dom(parser, elem, mem_res));
        }
        return array;
    }
    case Token::Type::Object: {
        JsonValue::Object object(mem_res);
        while (const auto key = parser.next()) {
            assert(key.type() == Token::Type::String);
            object.emplace(parser.parse_string(key), to_dom(parser, parser.next(), mem_res));
        }
        return object;
    }
    case Token::Type::Error:
    default:
        throw std::runtime_error(std::string(token.error_message()));
    }
}

void print_value(const JsonValue& value, size_t indent = 0)
{
    std::cout << std::string(4 * indent, ' ');
    switch (value.type()) {
    case JsonValue::Type::Null:
        std::cout << "null\n";
        break;
    case JsonValue::Type::Bool:
        std::cout << "bool: " << value.asBool() << "\n";
        break;
    case JsonValue::Type::Number:
        std::cout << "number: " << value.asNumber() << "\n";
        break;
    case JsonValue::Type::String:
        std::cout << "string: " << value.asString() << "\n";
        break;
    case JsonValue::Type::Array:
        std::cout << "array (" << value.size() << ")\n";
        for (const auto& elem : value.asArray()) {
            print_value(elem, indent + 1);
        }
        break;
    case JsonValue::Type::Object:
        std::cout << "object (" << value.size() << ")\n";
        for (const auto& [key, value] : value.asObject()) {
            std::cout << std::string(4 * (indent + 1), ' ') << "key: " << key << "\n";
            print_value(value, indent + 1);
        }
        break;
    default:
        assert(false && "Invalid JSON value type");
    }
}

std::string to_string(Token::Type type)
{
    switch (type) {
    case Token::Type::Null:
        return "Null";
    case Token::Type::Bool:
        return "Bool";
    case Token::Type::UInt:
        return "UInt";
    case Token::Type::Int:
        return "Int";
    case Token::Type::Float:
        return "Float";
    case Token::Type::String:
        return "String";
    case Token::Type::Array:
        return "Array";
    case Token::Type::Object:
        return "Object";
    case Token::Type::EndArray:
        return "EndArray";
    case Token::Type::EndObject:
        return "EndObject";
    case Token::Type::Eof:
        return "Eof";
    case Token::Type::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

std::string to_string(const Token& token)
{
    std::stringstream ss;
    if (token.type() == Token::Type::Error) {
        ss << "Token(type=Error, location=" << token.error_location() << ", message=\""
           << token.error_message() << "\")";
    } else {
        ss << "Token(type=" << to_string(token.type()) << ", string=\"" << token.string() << "\")";
    }
    return ss.str();
}

bool print_tree(Parser& parser, const Token& token, std::string indent = "")
{
    std::cout << indent;
    switch (token.type()) {
    case Token::Type::Null:
        std::cout << "null\n";
        break;
    case Token::Type::String:
        std::cout << "string: " << parser.parse_string(token) << "\n";
        break;
    case Token::Type::Int:
        std::cout << "int: " << parser.parse_int(token) << "\n";
        break;
    case Token::Type::UInt:
        std::cout << "uint: " << parser.parse_uint(token) << "\n";
        break;
    case Token::Type::Float:
        std::cout << "float: " << parser.parse_float(token) << "\n";
        break;
    case Token::Type::Bool:
        std::cout << "bool: " << parser.parse_bool(token) << "\n";
        break;
    case Token::Type::Array:
        std::cout << "array\n";
        while (const auto elem = parser.next()) {
            if (!print_tree(parser, elem, indent + "  ")) {
                return false;
            }
        }
        break;
    case Token::Type::Object:
        std::cout << "object\n";
        while (const auto key = parser.next()) {
            assert(key.type() == Token::Type::String);
            std::cout << indent + "  " << "key: " << parser.parse_string(key) << "\n";
            if (!print_tree(parser, parser.next(), indent + "  ")) {
                return false;
            }
        }
        break;
    case Token::Type::Error:
        std::cout << "Error: " << token.error_message() << std::endl;
        return false;
    default:
        std::cout << "Unexpected token: " << to_string(token) << std::endl;
        return false;
    }
    return true;
}

bool print_flat(Parser& parser)
{
    auto token = parser.next();
    while (token.type() != Token::Type::Eof && token.type() != Token::Type::Error) {
        std::cout << to_string(token) << std::endl;
        token = parser.next();
    }
    std::cout << to_string(token) << std::endl;
    if (token.type() == Token::Type::Error) {
        const auto ctx = get_context(parser.input(), token.error_location());
        std::cerr << "Line " << ctx.line_number << std::endl;
        std::cerr << ctx.line << std::endl;
        std::cerr << std::string(ctx.column, ' ') << "^" << std::endl;
    }
    return token.type() != Token::Type::Error;
}

size_t full_parse(Parser& parser)
{
    size_t v = 0; // silly var to avoid all work being optimized out
    auto token = parser.next();
    while (token.type() != Token::Type::Eof && token.type() != Token::Type::Error) {
        switch (token.type()) {
        case Token::Type::Null:
            break;
        case Token::Type::String:
            v += parser.parse_string(token).size();
            break;
        case Token::Type::Int:
            v += parser.parse_int(token) == 0;
            break;
        case Token::Type::UInt:
            v += parser.parse_uint(token) == 0;
            break;
        case Token::Type::Float:
            v += parser.parse_float(token) == 0.0f;
            break;
        case Token::Type::Bool:
            v += parser.parse_bool(token);
            break;
        default:
            break;
        }
        token = parser.next();
    }
    return token.type() == Token::Type::Error ? 0 : v;
}

struct Args {
    bool print_flat = false;
    bool print_tree = false;
    bool print_dom = false;
    std::optional<int> bench_sax;
    std::optional<int> bench_dom;
    std::string file;

    static std::optional<Args> parse(int argc, char** argv)
    {
        const std::vector<std::string> args(argv + 1, argv + argc);
        Args ret;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--print-flat") {
                ret.print_flat = true;
            } else if (args[i] == "--print-tree") {
                ret.print_tree = true;
            } else if (args[i] == "--print-dom") {
                ret.print_dom = true;
            } else if (args[i] == "--bench-sax") {
                if (i + 1 >= args.size()) {
                    std::cerr << "Missing iterations for --bench-sax" << std::endl;
                    return std::nullopt;
                }
                ret.bench_sax = std::stoi(args[i + 1]);
                i++;
            } else if (args[i] == "--bench-dom") {
                if (i + 1 >= args.size()) {
                    std::cerr << "Missing iterations for --bench-dom" << std::endl;
                    return std::nullopt;
                }
                ret.bench_dom = std::stoi(args[i + 1]);
                i++;
            } else if (args[i].starts_with("--")) {
                std::cerr << "Unknown flag '" << args[i] << "'" << std::endl;
                return std::nullopt;
            } else {
                if (!ret.file.empty()) {
                    std::cerr << "Too many positional arguments" << std::endl;
                    return std::nullopt;
                }
                ret.file = args[i];
            }
        }
        if (ret.file.empty()) {
            std::cerr << "Missing positional argument 'file'" << std::endl;
            return std::nullopt;
        }
        if (!ret.print_flat && !ret.print_tree && !ret.print_dom && !ret.bench_sax
            && !ret.bench_dom) {
            ret.print_flat = true; // default if nothing else is set
        }
        return ret;
    }
};

int print_flat(std::string& json)
{
    Parser parser(json);
    return print_flat(parser) ? 0 : 1;
}

int print_tree(std::string& json)
{
    Parser parser(json);
    return print_tree(parser, parser.next()) ? 0 : 1;
}

int print_dom(std::string& json)
{
    std::pmr::monotonic_buffer_resource pool;
    Parser parser(json);
    try {
        const auto dom = to_dom(parser, parser.next(), &pool);
        print_value(dom);
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << std::endl;
        return 1;
    }
}

auto delta_ms(std::chrono::high_resolution_clock::time_point start)
{
    const auto delta = std::chrono::high_resolution_clock::now() - start;
    return std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
}

int bench_sax(std::string& json, size_t num_iterations)
{
    // One test parse to catch errors
    Parser test_parser(json);
    if (full_parse(test_parser) == 0) {
        return 1;
    }

    const auto start = std::chrono::high_resolution_clock::now();
    // This doesn't work properly if strings need to be escaped
    for (size_t i = 0; i < num_iterations; ++i) {
        Parser bench_parser(json);
        if (full_parse(bench_parser) == static_cast<size_t>(-1)) {
            return 100;
        }
    }
    const auto delta = delta_ms(start);
    std::cerr << num_iterations << " iterations: " << delta << "ms" << std::endl;
    std::cerr << "Per parse: " << static_cast<float>(delta) / num_iterations << "ms" << std::endl;
    return 0;
}

int bench_dom(std::string& json, size_t num_iterations)
{
    // One test parse to catch errors
    Parser test_parser(json);
    if (full_parse(test_parser) == 0) {
        return 1;
    }

    std::pmr::monotonic_buffer_resource pool;
    const auto start = std::chrono::high_resolution_clock::now();
    // This doesn't work properly if strings need to be escaped
    for (size_t i = 0; i < num_iterations; ++i) {
        pool.release();
        Parser bench_parser(json);
        const auto dom = to_dom(bench_parser, bench_parser.next(), &pool);
        if (dom.size() == static_cast<size_t>(-1)) { // Just prevent dom from being optimized out
            return 100;
        }
    }
    const auto delta = delta_ms(start);
    std::cerr << num_iterations << " iterations: " << delta << "ms" << std::endl;
    std::cerr << "Per parse: " << static_cast<float>(delta) / num_iterations << "ms" << std::endl;
    return 0;
}

int main(int argc, char** argv)
{
    const auto args = Args::parse(argc, argv);
    if (!args) {
        std::cerr << "Usage: minijson-test [--print-flat] [--print-tree] [--print-dom] "
                     "[--bench-sax <iterations>] [--bench-dom <iterations>] <file>"
                  << std::endl;
        return 1;
    }

    FILE* f = std::fopen(args->file.c_str(), "rb");
    if (!f) {
        std::cerr << "Could not open file '" << args->file << "'" << std::endl;
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    const auto size = std::ftell(f);
    if (size < 0) {
        std::cerr << "Error getting file size" << std::endl;
        return 1;
    }
    std::fseek(f, 0, SEEK_SET);
    std::string json;
    json.resize(size);
    const auto readRes = std::fread(json.data(), 1, size, f);
    if (readRes != static_cast<size_t>(size)) {
        std::cerr << "Error reading file: " << readRes << std::endl;
        return 1;
    }

    if (args->print_flat) {
        return print_flat(json);
    }

    if (args->print_tree) {
        return print_tree(json);
    }

    if (args->print_dom) {
        return print_dom(json);
    }

    if (args->bench_sax) {
        return bench_sax(json, *args->bench_sax);
    }

    if (args->bench_dom) {
        return bench_dom(json, *args->bench_dom);
    }

    return 200;
}