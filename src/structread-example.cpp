#include <iostream>

#include <minijson2/minijson2.hpp>

using namespace minijson2;

struct Asset {
    std::string generator;
    std::string version;
    uint16_t num_version;
};
MJ2_TYPE_META(Asset, generator, version, num_version)

struct Scene {
    std::string name;
    float weight = 15.0f;
    std::vector<size_t> nodes;
    std::optional<size_t> camera;
};
MJ2_TYPE_META(Scene, name, weight, nodes, camera)
MJ2_OPTIONAL_FIELDS(Scene, weight)

struct Gltf {
    Asset asset;
    std::vector<Scene> scenes;
};
MJ2_TYPE_META(Gltf, asset, scenes)

template <typename T>
    requires requires(std::ostream& os, T a) { os << a; }
void print(const T& obj, const std::string& path = "")
{
    std::cout << path << ": " << obj << std::endl;
}

template <typename T>
void print(const std::vector<T>& v, const std::string& path = "")
{
    for (size_t i = 0; i < v.size(); ++i) {
        print(v[i], path + "[" + std::to_string(i) + "]");
    }
}

template <typename T>
void print(const std::optional<T>& v, const std::string& path = "")
{
    if (v) {
        print(*v, path);
    }
}

template <structread::has_type_meta T>
void print(const T& obj, const std::string& path = "")
{
    structread::for_each_field(obj, [&](std::string_view name, const auto& field) {
        print(field, path + "." + std::string(name));
    });
}

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

    structread::ParseContext ctx(input);
    Gltf gltf;
    if (!structread::from_json(gltf, ctx)) {
        std::cerr << "Error: " << ctx.error.value().message << std::endl;
        const auto err_ctx = get_context(ctx.parser.input(), ctx.error.value().location);
        std::cerr << "Line " << err_ctx.line_number << std::endl;
        std::cerr << err_ctx.line << std::endl;
        std::cerr << std::string(err_ctx.column, ' ') << "^" << std::endl;
        return 1;
    }

    print(gltf);
}