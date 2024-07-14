#include <iostream>
#include <map>
#include <memory_resource>
#include <optional>
#include <variant>

#include <minijson2/minijson2.hpp>

using namespace minijson2;

////////////////////////////////////////////////////////////

class Json {
public:
    struct Invalid { };
    struct Null { };
    using Bool = bool;
    using Number = double;
    using String = std::pmr::string;
    using Array = std::pmr::vector<Json>;
    // map instead of unordered_map, because it supports incomplete value types
    // We need a transparent comparator to enable .find with std::string_view
    using Object = std::pmr::map<std::pmr::string, Json, std::less<>>;

    enum class Type {
        Invalid = 0,
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Json() : value_(Invalid {}) { }
    Json(Null n) : value_(std::move(n)) { } // better move that empty struct! #highperformance
    Json(bool b) : value_(b) { }
    Json(double v) : value_(v) { }
    Json(String s) : value_(std::move(s)) { }
    Json(Array v) : value_(std::move(v)) { }
    Json(Object m) : value_(std::move(m)) { }

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
    bool isBool() const { return is<Bool>(); }
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

    static const Json& getNonExistent()
    {
        static Json v;
        return v;
    }

    const Json& operator[](std::string_view key) const
    {
        if (isObject()) {
            const auto& obj = as<Object>();
            const auto it = obj.find(key);
            if (it == obj.end()) {
                return getNonExistent();
            }
            return it->second;
        } else {
            return getNonExistent();
        }
    }

    const Json& operator[](size_t index) const
    {
        if (isArray() && index < size()) {
            return asArray()[index];
        } else {
            return getNonExistent();
        }
    }

private:
    std::variant<Invalid, Null, Bool, Number, String, Array, Object> value_;
};

Json to_dom(Parser& parser, const Token& token, std::pmr::memory_resource* mem_res)
{
    switch (token.type()) {
    case Token::Type::Null:
        return Json(Json::Null {});
    case Token::Type::String:
        return Json(Json::String(parser.parse_string(token), mem_res));
    case Token::Type::Int:
    case Token::Type::UInt:
    case Token::Type::Float:
        return Json(parser.parse_float(token));
    case Token::Type::Bool:
        return Json(parser.parse_bool(token));
    case Token::Type::Array: {
        Json::Array array(mem_res);
        while (const auto elem = parser.next()) {
            array.push_back(to_dom(parser, elem, mem_res));
        }
        return array;
    }
    case Token::Type::Object: {
        Json::Object object(mem_res);
        while (const auto key = parser.next()) {
            assert(key.type() == Token::Type::String);
            object.emplace(parser.parse_string(key), to_dom(parser, parser.next(), mem_res));
        }
        return object;
    }
    case Token::Type::Error: {
        const auto ctx = get_context(parser.input(), token.error_location());
        std::cerr << "Line " << ctx.line_number << std::endl;
        std::cerr << ctx.line << std::endl;
        std::cerr << std::string(ctx.column, ' ') << "^" << std::endl;
        throw std::runtime_error("Could not parse JSON");
    }
    default:
        throw std::runtime_error("Could not parse JSON");
    }
}

struct TrackProxy {
public:
    explicit TrackProxy(const Json& json, std::string path = "")
        : json_(json)
        , path_(std::move(path))
    {
    }

    TrackProxy operator[](std::string_view key) const
    {
        return TrackProxy(
            json_[key], !path_.empty() ? (path_ + "." + std::string(key)) : std::string(key));
    }

    TrackProxy operator[](size_t idx) const
    {
        return TrackProxy(json_[idx], path_ + "[" + std::to_string(idx) + "]");
    }

    const Json* operator->() const { return &json_; }

    const std::string& path() const { return path_; }

private:
    const Json& json_;
    std::string path_;
};

////////////////////////////////////////////////////////////

#define GET_OBJECT                                                                                 \
    if (!json->isObject()) {                                                                       \
        std::cerr << json.path() << " must be an object" << std::endl;                             \
        return false;                                                                              \
    }

#define GET_FIELD(obj, field)                                                                      \
    if (!from_json(obj.field, json[#field])) {                                                     \
        return false;                                                                              \
    }

bool from_json(bool& val, const TrackProxy& json)
{
    if (!json->isBool()) {
        std::cerr << json.path() << " must be a boolean" << std::endl;
        return false;
    }
    val = json->asBool();
    return true;
}

bool from_json(int64_t& val, const TrackProxy& json)
{
    if (!json->isNumber()) {
        std::cerr << json.path() << " must be an integer" << std::endl;
        return false;
    }
    const auto raw = json->asNumber();
    val = static_cast<int64_t>(raw);
    if (static_cast<double>(val) != raw) {
        std::cerr << json.path() << " must be an integer" << std::endl;
        return false;
    }
    return true;
}

bool from_json(uint64_t& val, const TrackProxy& json)
{
    if (!json->isNumber()) {
        return false;
        std::cerr << json.path() << " must be an unsigned integer" << std::endl;
    }
    const auto raw = json->asNumber();
    val = static_cast<uint64_t>(raw);
    if (static_cast<double>(val) != raw) {
        std::cerr << json.path() << " must be an unsigned integer" << std::endl;
        return false;
    }
    return true;
}

bool from_json(double& val, const TrackProxy& json)
{
    if (!json->isNumber()) {
        std::cerr << json.path() << " must be a float" << std::endl;
        return false;
    }
    val = json->asNumber();
    return true;
}

bool from_json(std::string& val, const TrackProxy& json)
{
    if (!json->isString()) {
        std::cerr << json.path() << " must be a string" << std::endl;
        return false;
    }
    val = json->asString();
    return true;
}

template <typename T>
bool from_json(std::optional<T>& opt, const TrackProxy& json)
{
    if (json->isValid()) {
        return from_json(opt.emplace(), json);
    }
    return true;
}

template <typename T>
bool from_json(std::vector<T>& vec, const TrackProxy& json)
{
    if (!json->isArray()) {
        std::cerr << json.path() << " must be a array" << std::endl;
        return false;
    }
    vec.resize(json->size());
    for (size_t i = 0; i < json->size(); ++i) {
        if (!from_json(vec[i], json[i])) {
            return false;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////

struct Asset {
    std::string generator;
    std::string version;
};

struct Scene {
    std::string name;
    std::vector<size_t> nodes;
    std::optional<size_t> camera;
};

struct Gltf {
    Asset asset;
    std::vector<Scene> scenes;
};

bool from_json(Asset& asset, const TrackProxy& json)
{
    GET_OBJECT;
    GET_FIELD(asset, generator);
    GET_FIELD(asset, version);
    return true;
}

bool from_json(Scene& scene, const TrackProxy& json)
{
    GET_OBJECT;
    GET_FIELD(scene, name);
    GET_FIELD(scene, nodes);
    GET_FIELD(scene, camera);
    return true;
}

bool from_json(Gltf& gltf, const TrackProxy& json)
{
    GET_OBJECT;
    GET_FIELD(gltf, asset);
    GET_FIELD(gltf, scenes);
    return true;
}

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

    std::pmr::monotonic_buffer_resource pool;
    Parser parser(input);
    const auto json = to_dom(parser, parser.next(), &pool);

    Gltf gltf;
    if (!from_json(gltf, TrackProxy(json))) {
        std::cerr << "Error reading document" << std::endl;
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
            std::cout << "scenes[" << s << "].nodes[" << n << "] : " << gltf.scenes[s].nodes[n]
                      << std::endl;
        }
    }
}