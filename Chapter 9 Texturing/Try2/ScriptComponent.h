#pragma once

#include <glm/glm.hpp>
#include <map>
#include <string>
#include <variant>

// Scene data only: no Lua registry references, functions or interpreter pointers.
using ScriptValue = std::variant<bool, double, std::string, glm::vec3>;
using ScriptParameters = std::map<std::string, ScriptValue>;

struct ScriptComponent
{
    std::string path; // UTF-8, relative to the asset root: Scripts/ or Games/<name>/Scripts/.
    bool enabled = true;
    ScriptParameters parameters;
};

inline const char* ScriptValueType(const ScriptValue& value)
{
    static constexpr const char* names[] = { "bool", "number", "string", "Vec3" };
    return names[value.index()];
}
