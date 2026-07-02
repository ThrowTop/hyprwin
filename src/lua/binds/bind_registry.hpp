#pragma once

#include <functional>
#include <unordered_map>

#include <lua.hpp>

#include "lua/binds/key_event.hpp"

namespace lua {

class BindRegistry {
  public:
    BindRegistry() = default;
    ~BindRegistry() = default;

    BindRegistry(const BindRegistry&) = delete;
    BindRegistry& operator=(const BindRegistry&) = delete;

    void Clear(lua_State* state) noexcept;
    bool Add(lua_State* state, const KeyEvent& event, int functionIndex) noexcept;
    [[nodiscard]] int Find(const KeyEvent& event) const noexcept;
    void ForEach(const std::function<void(const KeyEvent&)>& fn) const;

  private:
    std::unordered_map<KeyEvent, int, KeyEventHash> m_binds;
};

} // namespace lua
