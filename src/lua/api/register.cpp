#include "lua/api/internal.hpp"

#include "log/log.hpp"
#include "lua/util/stack.hpp"

namespace lua::api {

namespace {

    int LuaPrint(lua_State* state) {
        const int n = lua_gettop(state);
        std::string out;
        for (int i = 1; i <= n; ++i) {
            if (i > 1)
                out += '\t';
            lua_getglobal(state, "tostring");
            lua_pushvalue(state, i);
            lua_call(state, 1, 1);
            std::size_t len = 0;
            const char* s = lua_tolstring(state, -1, &len);
            if (s)
                out.append(s, len);
            lua_pop(state, 1);
        }
        logging::detail::viewer_print(std::move(out));
        return 0;
    }

} // namespace

void registerAll(lua_State* state, Context& context) {
    storeContext(state, context);
    color::registerApi(state);
    settings::registerApi(state);

    lua_newtable(state);
    core::registerApi(state);
    window::registerApi(state);
    monitor::registerApi(state);
    system::registerApi(state);
    fs::registerApi(state);
    input::registerApi(state);
    clipboard::registerApi(state);
    audio::registerApi(state);
    mouse_api::registerApi(state);
    settings::storeHwImpl(state);
    lua_pop(state, 1);

    lua_newtable(state);
    lua_newtable(state);
    util::setFn(state, "__index", settings::hwIndex);
    util::setFn(state, "__newindex", settings::hwNewIndex);
    lua_setmetatable(state, -2);
    lua_setglobal(state, "hw");

    lua_pushcfunction(state, LuaPrint);
    lua_setglobal(state, "print");
}

} // namespace lua::api
