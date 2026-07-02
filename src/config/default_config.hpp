#pragma once

namespace hw {

constexpr char kDefaultConfig[] = R"lua(
-- hyprwin.lua - HyprWin default config

hw.settings = {
    super          = "LWIN",
    border         = 3,
    colors         = {
        color.hex("00a2ff"),
        color.hex("ff00f7"),
    },
    gradient_angle = 45,
    rotating       = true,
    rotation_speed = 120,
    resize_corner  = "closest",
}

hw.bind("Q", function()
    local w = hw.window.at_cursor()
    if not w or not w:responsive() then return end
    w:close()
end)

hw.bind("SHIFT+Q", function()
    local w = hw.window.at_cursor()
    if not w then return end
    w:kill()
end)

hw.bind("F", function()
    local w = hw.window.at_cursor()
    if not w then return end
    if w:get_maximized() then w:restore() else w:maximize() end
end)

hw.bind("RETURN", function() hw.launch("wt.exe") end)
hw.bind("E",      function() hw.run("explorer.exe") end)
hw.bind("V",      function() hw.input.send("SUPER+V") end)
hw.bind("R",      function() hw.input.send("SUPER+R") end)
hw.bind("D",      function() hw.input.send("SUPER+D") end)
)lua";

} // namespace hw
