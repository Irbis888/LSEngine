#include "ScriptRuntime.h"
#include "AssetPaths.h"
// Lua is built as C++ (including exception unwinding), with matching core API linkage.
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    constexpr const char* VecMeta = "LSEngine.Vec3";
    constexpr const char* EntityMeta = "LSEngine.Entity";
    struct Stack
    {
        lua_State* state;
        int top;
        explicit Stack(lua_State* state) : state(state), top(lua_gettop(state)) {}
        ~Stack() { lua_settop(state, top); }
    };
    void Field(lua_State* state, int table, const char* name)
    {
        table = lua_absindex(state, table);
        lua_pushstring(state, name);
        lua_rawget(state, table);
    }
    double Number(lua_State* state, int index)
    {
        if (lua_type(state, index) != LUA_TNUMBER) throw std::runtime_error("Expected a finite number");
        const double value = lua_tonumber(state, index);
        if (!std::isfinite(value)) throw std::runtime_error("Expected a finite number");
        return value;
    }
    float Float(lua_State* state, int index)
    {
        const double value = Number(state, index);
        if (std::abs(value) > (std::numeric_limits<float>::max)()) throw std::runtime_error("Number exceeds float range");
        return static_cast<float>(value);
    }
    std::string String(lua_State* state, int index, size_t limit = 4096)
    {
        if (lua_type(state, index) != LUA_TSTRING) throw std::runtime_error("Expected a string");
        size_t length = 0;
        const char* value = lua_tolstring(state, index, &length);
        if (length > limit) throw std::runtime_error("String exceeds the API size limit");
        return { value, length };
    }
    glm::vec3 Vector(lua_State* state, int index)
    {
        const auto* value = static_cast<const glm::vec3*>(luaL_testudata(state, index, VecMeta));
        if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) || !std::isfinite(value->z))
            throw std::runtime_error("Expected a finite Vec3");
        return *value;
    }
    void PushVector(lua_State* state, glm::vec3 value)
    {
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            throw std::runtime_error("Vec3 arithmetic produced a non-finite result");
        new (lua_newuserdatauv(state, sizeof(glm::vec3), 0)) glm::vec3(value);
        luaL_setmetatable(state, VecMeta);
    }
    template<int (*Function)(lua_State*)> int Guard(lua_State* state)
    {
        try { return Function(state); }
        catch (const std::exception& error) { return luaL_error(state, "%s", error.what()); }
        // Do not catch Lua's own exception: it must unwind to lua_pcall.
    }
    void PushValue(lua_State* state, const ScriptValue& value)
    {
        std::visit([&](const auto& item)
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) lua_pushboolean(state, item);
            else if constexpr (std::is_same_v<T, double>) lua_pushnumber(state, item);
            else if constexpr (std::is_same_v<T, std::string>) lua_pushlstring(state, item.data(), item.size());
            else PushVector(state, item);
        }, value);
    }
    ScriptValue ReadValue(lua_State* state, int index)
    {
        switch (lua_type(state, index))
        {
        case LUA_TBOOLEAN: return lua_toboolean(state, index) != 0;
        case LUA_TNUMBER: return Number(state, index);
        case LUA_TSTRING: return String(state, index);
        case LUA_TUSERDATA: return Vector(state, index);
        default: throw std::runtime_error("Values must be bool, number, string or Vec3 (no nested tables/functions/entities)");
        }
    }
    ScriptParameters ReadParameters(lua_State* state, int index)
    {
        if (lua_isnil(state, index)) return {};
        if (!lua_istable(state, index)) throw std::runtime_error("Expected a flat table of named values");
        index = lua_absindex(state, index);
        ScriptParameters result;
        lua_pushnil(state);
        while (lua_next(state, index))
        {
            if (result.size() >= 64) throw std::runtime_error("At most 64 named values are supported");
            const auto key = String(state, -2, 128);
            if (key.empty() || key.find('\0') != std::string::npos) throw std::runtime_error("Invalid parameter name");
            result.emplace(key, ReadValue(state, -1));
            lua_pop(state, 1);
        }
        return result;
    }
    void PushParameters(lua_State* state, const ScriptParameters& parameters)
    {
        lua_createtable(state, 0, static_cast<int>(parameters.size()));
        for (const auto& [key, value] : parameters)
        {
            lua_pushlstring(state, key.data(), key.size());
            PushValue(state, value);
            lua_rawset(state, -3);
        }
    }
}

struct ScriptRuntime::Impl
{
    lua_State* state = nullptr;
    std::filesystem::path root;
    std::thread::id owner = std::this_thread::get_id();
    entt::registry* registry = nullptr;
    uint64_t generation = 1;
    bool callback = false, allowEdges = false, captured = false;
    entt::entity current = entt::null;
    InputState input{};
    uint32_t instructionLimit = 200000;
    lua_Integer randomSeed = 0;
    lua_Unsigned nextEnvironmentSeed = 0;
    int64_t remaining = 0;
    std::deque<Event> events;
    std::vector<EntityRef> destructions;
    std::vector<SpawnRequest> spawns;
    // Parsed templates for the current session. Bind clears it, so edited prefab files apply on the next Play.
    std::map<std::string, std::shared_ptr<SceneSerializer::EntityDescription>> prefabs;
    // Script text of the current session by canonical path. Spawns reuse it instead of re-reading
    // the disk; hot reload replaces an entry only after the new text has compiled.
    std::map<std::string, std::string> sources;

    explicit Impl(std::filesystem::path root) : root(std::filesystem::weakly_canonical(root))
    {
        state = luaL_newstate();
        if (!state) throw std::runtime_error("Could not create Lua interpreter");
        randomSeed = static_cast<lua_Integer>(luaL_makeseed(state));
        *static_cast<Impl**>(lua_getextraspace(state)) = this;
        luaL_requiref(state, "_G", luaopen_base, 1); lua_pop(state, 1);
        luaL_requiref(state, "math", luaopen_math, 1); lua_pop(state, 1);
        luaL_requiref(state, "string", luaopen_string, 1); lua_pop(state, 1);
        luaL_requiref(state, "table", luaopen_table, 1); lua_pop(state, 1);
        luaL_requiref(state, "utf8", luaopen_utf8, 1); lua_pop(state, 1);
        RegisterTypes();
    }
    ~Impl() { lua_close(state); }
    void CheckThread() const
    {
        if (std::this_thread::get_id() != owner) throw std::logic_error("Lua runtime is main-thread only");
    }
    static Impl& Self(lua_State* state) { return **static_cast<Impl**>(lua_getextraspace(state)); }
    void RequireCallback() const
    {
        CheckThread();
        if (!callback || !registry) throw std::runtime_error("Scene API is available only in script callbacks, not metadata/top-level code");
    }
    bool Valid(EntityRef value) const { return registry && value.generation == generation && registry->valid(value.entity); }
    EntityRef Ref(entt::entity entity) const { return { entity, generation }; }
    static EntityRef ReadEntity(lua_State* state, int index)
    {
        const auto* entity = static_cast<EntityRef*>(luaL_testudata(state, index, EntityMeta));
        if (!entity) throw std::runtime_error("Expected a safe Entity reference");
        return *entity;
    }
    entt::entity Entity(lua_State* state, int index = 1) const
    {
        RequireCallback();
        const auto value = ReadEntity(state, index);
        if (!Valid(value)) throw std::runtime_error("Entity is deleted or belongs to a closed scene/Play session");
        return value.entity;
    }
    void PushEntity(EntityRef value)
    {
        new (lua_newuserdatauv(state, sizeof(EntityRef), 0)) EntityRef(value);
        luaL_setmetatable(state, EntityMeta);
    }
    template<class Component> Component& ComponentOf(entt::entity entity) const
    {
        auto* value = registry->try_get<Component>(entity);
        if (!value)
        {
            for (const auto& spawn : spawns)
                if (spawn.entity.entity == entity)
                    throw std::runtime_error("Spawned entity receives its components after the current callbacks; use it from the next update");
            throw std::runtime_error("Entity is missing the requested component");
        }
        return *value;
    }
    static int MakeVector(lua_State* state)
    {
        PushVector(state, { lua_gettop(state) >= 1 ? Float(state, 1) : 0,
            lua_gettop(state) >= 2 ? Float(state, 2) : 0, lua_gettop(state) >= 3 ? Float(state, 3) : 0 });
        return 1;
    }
    static int VectorIndex(lua_State* state)
    {
        const auto value = Vector(state, 1);
        const auto key = String(state, 2);
        if (key == "x") lua_pushnumber(state, value.x);
        else if (key == "y") lua_pushnumber(state, value.y);
        else if (key == "z") lua_pushnumber(state, value.z);
        else throw std::runtime_error("Vec3 has only x, y and z fields");
        return 1;
    }
    static int VectorSet(lua_State* state)
    {
        Vector(state, 1);
        auto& value = *static_cast<glm::vec3*>(lua_touserdata(state, 1));
        const auto key = String(state, 2);
        const float number = Float(state, 3);
        if (key == "x") value.x = number;
        else if (key == "y") value.y = number;
        else if (key == "z") value.z = number;
        else throw std::runtime_error("Vec3 has only x, y and z fields");
        return 0;
    }
    static int VectorAdd(lua_State* s) { PushVector(s, Vector(s, 1) + Vector(s, 2)); return 1; }
    static int VectorSub(lua_State* s) { PushVector(s, Vector(s, 1) - Vector(s, 2)); return 1; }
    static int VectorNeg(lua_State* s) { PushVector(s, -Vector(s, 1)); return 1; }
    static int VectorMul(lua_State* s)
    {
        PushVector(s, lua_type(s, 1) == LUA_TNUMBER ? Float(s, 1) * Vector(s, 2) : Vector(s, 1) * Float(s, 2));
        return 1;
    }
    static int VectorString(lua_State* s)
    {
        const auto value = Vector(s, 1);
        lua_pushfstring(s, "Vec3(%f, %f, %f)", double(value.x), double(value.y), double(value.z)); return 1;
    }
    static int EntityIndex(lua_State* s)
    {
        ReadEntity(s, 1);
        const auto name = String(s, 2, 128);
        // Do not expose the shared metatable through entity.__index.
        if (name.starts_with("__")) { lua_pushnil(s); return 1; }
        luaL_getmetatable(s, EntityMeta);
        Field(s, -1, name.c_str());
        return 1;
    }
    static int EntityString(lua_State* s)
    {
        const auto value = ReadEntity(s, 1);
        const auto text = "Entity(" + std::to_string(entt::to_integral(value.entity)) + ")";
        lua_pushlstring(s, text.data(), text.size()); return 1;
    }
    static int EntityValid(lua_State* s)
    {
        auto& self = Self(s);
        self.RequireCallback();
        lua_pushboolean(s, self.Valid(ReadEntity(s, 1)));
        return 1;
    }
    static int EntityEqual(lua_State* s)
    {
        const auto a = ReadEntity(s, 1), b = ReadEntity(s, 2);
        lua_pushboolean(s, a.entity == b.entity && a.generation == b.generation);
        return 1;
    }
    static int Name(lua_State* s)
    {
        auto& self = Self(s);
        const auto& name = self.ComponentOf<TagComponent>(self.Entity(s)).tag;
        lua_pushlstring(s, name.data(), name.size()); return 1;
    }
    static int Find(lua_State* s)
    {
        auto& self = Self(s); self.RequireCallback();
        const auto name = String(s, 1);
        for (auto entity : self.registry->view<TagComponent>())
            if (self.registry->get<TagComponent>(entity).tag == name) { self.PushEntity(self.Ref(entity)); return 1; }
        lua_pushnil(s); return 1;
    }
    // Entity.Spawn(path, position[, overrides]): overrides replace the prefab's script parameters.
    static int SpawnPrefab(lua_State* s)
    {
        auto& self = Self(s); self.RequireCallback();
        const auto path = AssetPaths::PrefabPath(self.root, String(s, 1));
        const auto position = Vector(s, 2);
        auto overrides = lua_gettop(s) >= 3 ? ReadParameters(s, 3) : ScriptParameters{};
        auto& prefab = self.prefabs[path];
        if (!prefab)
        {
            const auto tag = AssetPaths::ToUtf8(AssetPaths::FromUtf8(path).stem());
            prefab = std::make_shared<SceneSerializer::EntityDescription>(
                SceneSerializer::ReadPrefab(AssetPaths::Resolve(self.root, path), tag));
        }
        if (!overrides.empty() && !prefab->script) throw std::runtime_error(path + " has no script for parameter overrides");
        if (self.spawns.size() >= 4096) throw std::runtime_error("Deferred spawn queue is full");
        const auto entity = self.registry->create(); // ID only: no component pool changes inside callbacks.
        self.spawns.push_back({ self.Ref(entity), path, prefab, position, std::move(overrides) });
        self.PushEntity(self.Ref(entity));
        return 1;
    }
    template<int Which, bool Set> static int Transform(lua_State* s)
    {
        auto& self = Self(s);
        auto& t = self.ComponentOf<TransformComponent>(self.Entity(s));
        glm::vec3& value = Which == 0 ? t.position : Which == 1 ? t.rotation : t.scale;
        if constexpr (Set) { value = Vector(s, 2); return 0; }
        else { PushVector(s, value); return 1; }
    }
    static int HasBody(lua_State* s)
    {
        auto& self = Self(s); lua_pushboolean(s, self.registry && self.registry->all_of<RigidbodyComponent>(self.Entity(s))); return 1;
    }
    static int GetVelocity(lua_State* s)
    {
        auto& self = Self(s); PushVector(s, self.ComponentOf<RigidbodyComponent>(self.Entity(s)).velocity); return 1;
    }
    static int SetVelocity(lua_State* s)
    {
        auto& self = Self(s); self.ComponentOf<RigidbodyComponent>(self.Entity(s)).velocity = Vector(s, 2); return 0;
    }
    static int Impulse(lua_State* s)
    {
        auto& self = Self(s);
        auto& body = self.ComponentOf<RigidbodyComponent>(self.Entity(s));
        const auto impulse = Vector(s, 2);
        if (body.IsDynamic())
        {
            const auto velocity = body.velocity + impulse * body.InverseMass();
            if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
                throw std::runtime_error("Impulse produced a non-finite velocity");
            body.velocity = velocity;
        }
        lua_pushboolean(s, body.IsDynamic()); return 1;
    }
    static int Destroy(lua_State* s)
    {
        auto& self = Self(s); const auto entity = self.Entity(s);
        if (self.destructions.size() >= 4096) throw std::runtime_error("Deferred destruction queue is full");
        self.destructions.push_back(self.Ref(entity)); return 0;
    }
    static int Send(lua_State* s)
    {
        auto& self = Self(s); const auto entity = self.Entity(s);
        const auto name = String(s, 2, 128);
        if (name.empty()) throw std::runtime_error("Event name must not be empty");
        auto payload = lua_gettop(s) >= 3 ? ReadParameters(s, 3) : ScriptParameters{};
        const auto* component = self.registry->try_get<ScriptComponent>(entity);
        if (!component || !component->enabled) { lua_pushboolean(s, false); return 1; }
        if (self.events.size() >= 4096) throw std::runtime_error("Script event queue is full");
        self.events.push_back({ self.Ref(entity), self.Ref(self.current), name, std::move(payload) });
        lua_pushboolean(s, true); return 1;
    }
    template<int Edge> static int Key(lua_State* s)
    {
        auto& self = Self(s); self.RequireCallback();
        const auto key = Number(s, 1);
        if (key < 0 || key > 255 || std::floor(key) != key) throw std::runtime_error("Key must be an integer in [0,255]");
        const int code = static_cast<int>(key);
        const bool value = Edge == 0 ? self.input.keys[code] : self.allowEdges &&
            (Edge == 1 ? self.input.keysPressed[code] : self.input.keysReleased[code]);
        lua_pushboolean(s, !self.captured && value); return 1;
    }
    static int Log(lua_State* s)
    {
        auto& self = Self(s);
        std::ostringstream message;
        for (int i = 1; i <= lua_gettop(s); ++i)
        {
            if (i > 1) message << ' ';
            if (lua_type(s, i) == LUA_TSTRING) message << String(s, i);
            else if (lua_type(s, i) == LUA_TNUMBER) message << Number(s, i);
            else if (lua_type(s, i) == LUA_TBOOLEAN) message << (lua_toboolean(s, i) ? "true" : "false");
            else message << luaL_typename(s, i);
        }
        const auto* tag = self.registry && self.registry->valid(self.current) ? self.registry->try_get<TagComponent>(self.current) : nullptr;
        std::cout << "[Lua][" << (tag ? tag->tag : "metadata") << "] " << message.str() << std::endl;
        return 0;
    }
    void Function(const char* name, lua_CFunction function)
    { lua_pushcfunction(state, function); lua_setfield(state, -2, name); }
    void RegisterTypes()
    {
        luaL_newmetatable(state, VecMeta);
        Function("__index", Guard<VectorIndex>); Function("__newindex", Guard<VectorSet>);
        Function("__add", Guard<VectorAdd>); Function("__sub", Guard<VectorSub>);
        Function("__unm", Guard<VectorNeg>); Function("__mul", Guard<VectorMul>);
        Function("__tostring", Guard<VectorString>);
        lua_pushliteral(state, "Vec3"); lua_setfield(state, -2, "__metatable"); lua_pop(state, 1);
        luaL_newmetatable(state, EntityMeta);
        Function("__index", Guard<EntityIndex>); Function("__tostring", Guard<EntityString>);
        lua_pushliteral(state, "Entity"); lua_setfield(state, -2, "__metatable");
        Function("__eq", Guard<EntityEqual>);
        Function("IsValid", Guard<EntityValid>); Function("GetName", Guard<Name>);
        Function("GetPosition", Guard<Transform<0, false>>); Function("SetPosition", Guard<Transform<0, true>>);
        Function("GetRotation", Guard<Transform<1, false>>); Function("SetRotation", Guard<Transform<1, true>>);
        Function("GetScale", Guard<Transform<2, false>>); Function("SetScale", Guard<Transform<2, true>>);
        Function("HasRigidbody", Guard<HasBody>); Function("GetVelocity", Guard<GetVelocity>);
        Function("SetVelocity", Guard<SetVelocity>); Function("ApplyImpulse", Guard<Impulse>);
        Function("Destroy", Guard<Destroy>); Function("SendEvent", Guard<Send>);
        lua_pop(state, 1);
    }
    void Environment()
    {
        lua_newtable(state);
        const int env = lua_gettop(state);
        for (const auto* name : { "assert", "error", "ipairs", "next", "pairs", "select", "tonumber", "tostring", "type" })
        { lua_getglobal(state, name); lua_setfield(state, env, name); }
        for (const auto* library : { "math", "string", "table", "utf8" })
        {
            if (std::string_view(library) == "math")
            {
                // math.random/randomseed capture mutable RNG userdata. A shallow
                // table copy would accidentally share that state between scripts.
                luaopen_math(state);
                // Lua's automatic seed uses seconds and a stack address, both of
                // which can be identical for every instance in a spawn burst.
                // Keep RNGs private, but give each environment a distinct seed pair.
                lua_getfield(state, -1, "randomseed");
                lua_pushinteger(state, randomSeed);
                lua_pushinteger(state, static_cast<lua_Integer>(++nextEnvironmentSeed));
                lua_call(state, 2, 0);
                lua_setfield(state, env, library);
                continue;
            }
            lua_getglobal(state, library);
            const int source = lua_gettop(state);
            lua_newtable(state);
            lua_pushnil(state);
            while (lua_next(state, source))
            {
                // string.dump permits exporting bytecode; the v1 loader accepts text only.
                if (!(std::string_view(library) == "string" && lua_type(state, -2) == LUA_TSTRING && String(state, -2) == "dump"))
                {
                    lua_pushvalue(state, -2); lua_pushvalue(state, -2); lua_rawset(state, source + 1);
                }
                lua_pop(state, 1);
            }
            lua_setfield(state, env, library); lua_pop(state, 1);
        }
        lua_pushvalue(state, env); lua_setfield(state, env, "_G");
        Function("Vec3", Guard<MakeVector>); Function("Log", Guard<Log>); Function("print", Guard<Log>);
        lua_newtable(state);
        Function("FindByName", Guard<Find>); Function("Spawn", Guard<SpawnPrefab>); lua_setfield(state, env, "Entity");
        lua_newtable(state);
        Function("IsKeyDown", Guard<Key<0>>); Function("IsKeyPressed", Guard<Key<1>>); Function("IsKeyReleased", Guard<Key<2>>);
        lua_setfield(state, env, "Input");
        lua_newtable(state);
        for (char key = 'A'; key <= 'Z'; ++key)
        { char name[] = { key, 0 }; lua_pushinteger(state, key); lua_setfield(state, -2, name); }
        for (const auto& [name, key] : std::initializer_list<std::pair<const char*, int>>{
            {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT},
            {"Up", VK_UP}, {"Down", VK_DOWN}, {"Delete", VK_DELETE}, {"Shift", VK_SHIFT}, {"Escape", VK_ESCAPE} })
        { lua_pushinteger(state, key); lua_setfield(state, -2, name); }
        lua_setfield(state, env, "Key");
    }
    static int Traceback(lua_State* s)
    {
        const char* error = lua_tostring(s, 1);
        luaL_traceback(s, s, error ? error : "Lua error (non-string error object)", 1);
        return 1;
    }
    static void Hook(lua_State* s, lua_Debug*)
    {
        auto& self = Self(s);
        self.remaining -= 100;
        if (self.remaining <= 0) luaL_error(s, "Lua instruction limit exceeded");
    }
    std::string ProtectedCall(int arguments, int results)
    {
        const int function = lua_gettop(state) - arguments;
        lua_pushcfunction(state, Traceback); lua_insert(state, function);
        struct HookScope
        {
            Impl& self;
            explicit HookScope(Impl& self) : self(self)
            { self.remaining = self.instructionLimit; lua_sethook(self.state, Hook, LUA_MASKCOUNT, 100); }
            ~HookScope() { lua_sethook(self.state, nullptr, 0, 0); self.remaining = 0; }
        } hook(*this);
        const int status = lua_pcall(state, arguments, results, function);
        std::string error;
        if (status != LUA_OK)
        { const char* message = lua_tostring(state, -1); error = message ? message : "Lua error"; lua_pop(state, 1); }
        lua_remove(state, function);
        return error;
    }
    void Load(const std::string& path)
    {
        const auto canonical = AssetPaths::ScriptPath(root, path);
        auto cached = sources.find(canonical);
        if (cached == sources.end())
        {
            const auto filename = AssetPaths::Resolve(root, canonical);
            std::ifstream file(filename, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open Lua file: " + canonical);
            if (std::filesystem::file_size(filename) > 1024 * 1024) throw std::runtime_error("Lua file exceeds 1 MiB");
            cached = sources.emplace(canonical, StripBom({ std::istreambuf_iterator<char>(file), {} })).first;
        }
        const auto& source = cached->second;
        const auto chunk = "@" + canonical;
        if (luaL_loadbufferx(state, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK)
            throw std::runtime_error(lua_tostring(state, -1));
        Environment();
        lua_setupvalue(state, -2, 1); // Every loaded text chunk has its own _ENV.
        const auto error = ProtectedCall(0, 1);
        if (!error.empty()) throw std::runtime_error(error);
        if (!lua_istable(state, -1)) throw std::runtime_error("Lua file must return a script table");
        for (const char* name : { "OnInit", "OnUpdate", "OnFixedUpdate", "OnDestroy", "OnEvent" })
        {
            Field(state, -1, name);
            const bool valid = lua_isnil(state, -1) || lua_isfunction(state, -1);
            lua_pop(state, 1);
            if (!valid) throw std::runtime_error(std::string(name) + " must be a function or nil");
        }
    }
    std::string Diagnostic(entt::entity entity, const std::string& path, const char* method, const std::string& error)
    {
        const auto* tag = registry && registry->valid(entity) ? registry->try_get<TagComponent>(entity) : nullptr;
        return "[Lua] " + path + " | entity=" + (tag ? tag->tag : "<metadata>") +
            " (" + std::to_string(entt::to_integral(entity)) + ") | " + method + ": " + error;
    }
};

ScriptRuntime::ScriptRuntime(std::filesystem::path assetRoot) : mImpl(std::make_unique<Impl>(std::move(assetRoot))) {}
ScriptRuntime::~ScriptRuntime() = default;
void ScriptRuntime::Bind(entt::registry& registry)
{ mImpl->CheckThread(); mImpl->registry = &registry; ++mImpl->generation; ClearCommands(); mImpl->prefabs.clear(); mImpl->sources.clear(); }
void ScriptRuntime::Unbind()
{ mImpl->CheckThread(); ClearCommands(); mImpl->registry = nullptr; ++mImpl->generation; }
void ScriptRuntime::SetInput(const InputState& input, bool captured, bool allowEdges)
{ mImpl->CheckThread(); mImpl->input = input; mImpl->captured = captured; mImpl->allowEdges = allowEdges; }
ScriptRuntime::Description ScriptRuntime::Describe(const std::string& path)
{
    auto& impl = *mImpl; impl.CheckThread(); Stack stack(impl.state);
    Description result;
    try
    {
        impl.Load(path);
        Field(impl.state, -1, "parameters");
        result.defaults = ReadParameters(impl.state, -1);
    }
    catch (const std::exception& error) { result.error = impl.Diagnostic(entt::null, path, "Describe", error.what()); }
    return result;
}
ScriptRuntime::Instance ScriptRuntime::Create(entt::entity entity, const ScriptComponent& component)
{
    auto& impl = *mImpl; impl.CheckThread(); Stack stack(impl.state);
    Instance instance; instance.entity = entity; instance.path = component.path;
    try
    {
        impl.Load(component.path);
        Field(impl.state, -1, "parameters");
        auto defaults = ReadParameters(impl.state, -1);
        lua_pop(impl.state, 1);
        for (const auto& [name, value] : component.parameters)
        {
            const auto it = defaults.find(name);
            if (it == defaults.end()) throw std::runtime_error("Unknown parameter '" + name + "'; reload/reset parameters in Edit");
            if (it->second.index() != value.index())
                throw std::runtime_error("Parameter '" + name + "' expects " + ScriptValueType(it->second));
            // Validate C++/serialized values through the same finite/size checks.
            PushValue(impl.state, value); ReadValue(impl.state, -1); lua_pop(impl.state, 1);
            it->second = value;
        }
        PushParameters(impl.state, defaults); lua_setfield(impl.state, -2, "params");
        impl.PushEntity(impl.Ref(entity)); lua_setfield(impl.state, -2, "entity");
        instance.reference = luaL_ref(impl.state, LUA_REGISTRYINDEX);
    }
    catch (const std::exception& error)
    {
        instance.failed = true;
        instance.error = impl.Diagnostic(entity, component.path, "Load", error.what());
        std::cerr << instance.error << std::endl;
    }
    return instance;
}
bool ScriptRuntime::Call(Instance& instance, const char* callback, float dt, const Event* event)
{
    auto& impl = *mImpl; impl.CheckThread(); Stack stack(impl.state);
    if (instance.reference == LUA_NOREF || (instance.failed && std::string_view(callback) != "OnDestroy")) return false;
    lua_rawgeti(impl.state, LUA_REGISTRYINDEX, instance.reference);
    Field(impl.state, -1, callback);
    if (lua_isnil(impl.state, -1)) return true;
    lua_pushvalue(impl.state, -2);
    int arguments = 1;
    if (event)
    {
        lua_pushlstring(impl.state, event->name.data(), event->name.size());
        PushParameters(impl.state, event->payload);
        impl.PushEntity(event->sender);
        arguments += 3;
    }
    else if (std::string_view(callback) == "OnUpdate" || std::string_view(callback) == "OnFixedUpdate")
    { lua_pushnumber(impl.state, dt); ++arguments; }
    struct Context
    {
        Impl& impl;
        bool edges;
        Context(Impl& impl, entt::entity entity, const char* method) : impl(impl), edges(impl.allowEdges)
        { impl.callback = true; impl.current = entity; impl.allowEdges = edges && std::string_view(method) == "OnUpdate"; }
        ~Context() { impl.callback = false; impl.current = entt::null; impl.allowEdges = edges; }
    } context(impl, instance.entity, callback);
    const auto error = impl.ProtectedCall(arguments, 0);
    if (error.empty()) return true;
    instance.failed = true;
    instance.error = impl.Diagnostic(instance.entity, instance.path, callback, error);
    std::cerr << instance.error << std::endl;
    return false;
}
void ScriptRuntime::Release(Instance& instance)
{
    mImpl->CheckThread();
    if (instance.reference != LUA_NOREF) luaL_unref(mImpl->state, LUA_REGISTRYINDEX, instance.reference);
    instance.reference = LUA_NOREF;
}
void ScriptRuntime::ClearCommands()
{ mImpl->CheckThread(); mImpl->events.clear(); mImpl->destructions.clear(); mImpl->spawns.clear(); }
std::deque<ScriptRuntime::Event> ScriptRuntime::TakeEvents()
{ mImpl->CheckThread(); std::deque<Event> events; events.swap(mImpl->events); return events; }
std::vector<ScriptRuntime::EntityRef> ScriptRuntime::TakeDestructions()
{ mImpl->CheckThread(); std::vector<EntityRef> entities; entities.swap(mImpl->destructions); return entities; }
std::vector<ScriptRuntime::SpawnRequest> ScriptRuntime::TakeSpawns()
{ mImpl->CheckThread(); std::vector<SpawnRequest> spawns; spawns.swap(mImpl->spawns); return spawns; }
bool ScriptRuntime::Valid(EntityRef entity) const { mImpl->CheckThread(); return mImpl->Valid(entity); }
ScriptRuntime::EntityRef ScriptRuntime::Reference(entt::entity entity) const { mImpl->CheckThread(); return mImpl->Ref(entity); }
const std::filesystem::path& ScriptRuntime::GetAssetRoot() const { return mImpl->root; }
void ScriptRuntime::SetInstructionLimit(uint32_t limit)
{
    mImpl->CheckThread();
    if (limit < 100 || limit > 10000000) throw std::invalid_argument("Lua instruction limit must be in [100,10000000]");
    mImpl->instructionLimit = limit;
}
std::optional<std::string> ScriptRuntime::SetSource(const std::string& path, std::optional<std::string> source)
{
    auto& impl = *mImpl; impl.CheckThread();
    const auto canonical = AssetPaths::ScriptPath(impl.root, path);
    std::optional<std::string> previous;
    if (const auto it = impl.sources.find(canonical); it != impl.sources.end())
    { previous = std::move(it->second); impl.sources.erase(it); }
    if (source) impl.sources.emplace(canonical, StripBom(std::move(*source)));
    return previous;
}
void ScriptRuntime::ForgetSource(const std::string& path)
{
    mImpl->CheckThread();
    try { mImpl->sources.erase(AssetPaths::ScriptPath(mImpl->root, path)); }
    catch (const std::exception&) {} // An invalid path has no cached text; Load reports it.
}
std::string ScriptRuntime::CheckSyntax(std::string_view source, const std::string& path)
{
    // A separate state: lua_State objects share nothing, so a worker can compile while the
    // engine's interpreter keeps running on the main thread.
    lua_State* state = luaL_newstate();
    if (!state) return "Could not create Lua interpreter for validation";
    const auto chunk = "@" + path;
    std::string error;
    if (luaL_loadbufferx(state, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK)
    { const char* message = lua_tostring(state, -1); error = message ? message : "Lua syntax error"; }
    lua_close(state);
    return error;
}
std::string ScriptRuntime::StripBom(std::string source)
{
    if (source.starts_with("\xEF\xBB\xBF")) source.erase(0, 3); // UTF-8 BOM from Windows editors.
    return source;
}
std::vector<std::string> ScriptRuntime::GetPrefabPaths() const
{
    mImpl->CheckThread();
    std::vector<std::string> paths;
    for (const auto& [path, prefab] : mImpl->prefabs) if (prefab) paths.push_back(path);
    return paths;
}
void ScriptRuntime::SetPrefab(const std::string& path, std::shared_ptr<SceneSerializer::EntityDescription> prefab)
{
    mImpl->CheckThread();
    mImpl->prefabs[AssetPaths::PrefabPath(mImpl->root, path)] = std::move(prefab);
}
