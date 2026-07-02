#include "lua/binds/bind_registry.hpp"

namespace lua {

void BindRegistry::Clear(lua_State* state) noexcept {
    if (state) {
        for (const auto& [event, ref] : m_binds) {
            static_cast<void>(event);
            luaL_unref(state, LUA_REGISTRYINDEX, ref);
        }
    }
    m_binds.clear();
}

bool BindRegistry::Add(lua_State* state, const KeyEvent& event, int functionIndex) noexcept {
    if (!state) {
        return false;
    }

    if (const auto it = m_binds.find(event); it != m_binds.end()) {
        luaL_unref(state, LUA_REGISTRYINDEX, it->second);
        m_binds.erase(it);
    }

    lua_pushvalue(state, functionIndex);
    const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        return false;
    }

    m_binds.emplace(event, ref);
    return true;
}

int BindRegistry::Find(const KeyEvent& event) const noexcept {
    const auto it = m_binds.find(event);
    return it == m_binds.end() ? LUA_NOREF : it->second;
}

void BindRegistry::ForEach(const std::function<void(const KeyEvent&)>& fn) const {
    for (const auto& [event, ref] : m_binds) {
        static_cast<void>(ref);
        fn(event);
    }
}

} // namespace lua
