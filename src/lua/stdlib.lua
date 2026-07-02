local point_mt = {
    __tostring = function(p) return ("point(%d, %d)"):format(p.x, p.y) end,
    __add = function(a, b) return point(a.x+b.x, a.y+b.y) end,
    __sub = function(a, b) return point(a.x-b.x, a.y-b.y) end,
    __mul = function(a, b)
        if type(a) == "number" then return point(a*b.x, a*b.y) end
        return point(a.x*b, a.y*b)
    end,
    __div  = function(a, b) return point(math.floor(a.x/b), math.floor(a.y/b)) end,
    __unm  = function(a) return point(-a.x, -a.y) end,
    __eq   = function(a, b) return a.x == b.x and a.y == b.y end,
    offset   = function(self, dx, dy) return point(self.x+dx, self.y+dy) end,
    length   = function(self) return math.sqrt(self.x*self.x + self.y*self.y) end,
    distance = function(self, o) local dx,dy = self.x-o.x, self.y-o.y; return math.sqrt(dx*dx+dy*dy) end,
    clamp    = function(self, lo, hi)
        return point(math.max(lo.x, math.min(hi.x, self.x)),
                     math.max(lo.y, math.min(hi.y, self.y)))
    end,
}
point_mt.__index = point_mt

local rect_mt = {
    __tostring = function(r) return ("rect(%d, %d, %d, %d)"):format(r.x, r.y, r.x2, r.y2) end,
    __add = function(a, b) return rect(a.x+b.x, a.y+b.y, a.x2+b.x2, a.y2+b.y2) end,
    __sub = function(a, b) return rect(a.x-b.x, a.y-b.y, a.x2-b.x2, a.y2-b.y2) end,
    __mul = function(a, b)
        if type(a) == "number" then return rect(a*b.x, a*b.y, a*b.x2, a*b.y2) end
        return rect(a.x*b, a.y*b, a.x2*b, a.y2*b)
    end,
    __div = function(a, b)
        return rect(math.floor(a.x/b), math.floor(a.y/b), math.floor(a.x2/b), math.floor(a.y2/b))
    end,
    __eq = function(a, b)
        return a.x == b.x and a.y == b.y and a.x2 == b.x2 and a.y2 == b.y2
    end,
    offset    = function(self, dx, dy) return rect(self.x+dx, self.y+dy, self.x2+dx, self.y2+dy) end,
    inset     = function(self, x, y) y = y ~= nil and y or x; return rect(self.x+x, self.y+y, self.x2-x, self.y2-y) end,
    grow      = function(self, n) return rect(self.x-n, self.y-n, self.x2+n, self.y2+n) end,
    pos       = function(self) return point(self.x, self.y) end,
    size      = function(self) return point(self.w, self.h) end,
    center    = function(self) return point(self.x + math.floor(self.w/2), self.y + math.floor(self.h/2)) end,
    with_pos  = function(self, x, y) return rect(x, y, x+self.w, y+self.h) end,
    with_size = function(self, w, h) return rect(self.x, self.y, self.x+w, self.y+h) end,
    aspect    = function(self) return self.w / self.h end,
    is_empty  = function(self) return self.w <= 0 or self.h <= 0 end,
    contains  = function(self, p) return p.x >= self.x and p.x < self.x2 and p.y >= self.y and p.y < self.y2 end,
    intersects = function(self, o) return self.x < o.x2 and self.x2 > o.x and self.y < o.y2 and self.y2 > o.y end,
    intersection = function(self, o)
        local x, y = math.max(self.x, o.x), math.max(self.y, o.y)
        local x2, y2 = math.min(self.x2, o.x2), math.min(self.y2, o.y2)
        if x >= x2 or y >= y2 then return nil end
        return rect(x, y, x2, y2)
    end,
    union = function(self, o)
        return rect(math.min(self.x, o.x), math.min(self.y, o.y),
                    math.max(self.x2, o.x2), math.max(self.y2, o.y2))
    end,
    clamp = function(self, b)
        local x = math.max(b.x, math.min(b.x2 - self.w, self.x))
        local y = math.max(b.y, math.min(b.y2 - self.h, self.y))
        return rect(x, y, x+self.w, y+self.h)
    end,
    center_in = function(self, b)
        local x = b.x + math.floor((b.w - self.w) / 2)
        local y = b.y + math.floor((b.h - self.h) / 2)
        return rect(x, y, x+self.w, y+self.h)
    end,
}
rect_mt.__index = function(self, key)
    if key == "w" then return self.x2 - self.x end
    if key == "h" then return self.y2 - self.y end
    return rect_mt[key]
end
rect_mt.__newindex = function(self, key, value)
    if key == "w" or key == "h" then
        error("rect." .. key .. " is read-only", 2)
    end
    rawset(self, key, value)
end

point = setmetatable({}, {
    __call = function(_, x, y)
        return setmetatable({x=x, y=y}, point_mt)
    end,
})

rect = setmetatable({
    xywh = function(x, y, w, h) return rect(x, y, x+w, y+h) end,
}, {
    __call = function(_, x, y, x2, y2)
        return setmetatable({x=x, y=y, x2=x2, y2=y2}, rect_mt)
    end,
})

-- math extensions
math.clamp = function(v, lo, hi) return math.max(lo, math.min(hi, v)) end
math.round = function(v) return math.floor(v + 0.5) end
math.lerp  = function(a, b, t) return a + (b - a) * t end
math.sign = function(x)
    return x < 0 and -1 or x > 0 and 1 or 0
end

-- string extensions
string.starts_with  = function(s, prefix) return s:sub(1, #prefix) == prefix end
string.ends_with    = function(s, suffix) return suffix == "" or s:sub(-#suffix) == suffix end
string.matches_any  = function(s, patterns)
    for _, p in ipairs(patterns) do
        if s:match(p) then return true end
    end
    return false
end

-- table extensions
table.merge = function(a, b)
    local r = {}
    for k, v in pairs(a) do r[k] = v end
    for k, v in pairs(b) do r[k] = v end
    return r
end
table.map = function(t, fn)
    local r = {}
    for i, v in ipairs(t) do r[i] = fn(v) end
    return r
end
table.filter = function(t, fn)
    local r = {}
    for _, v in ipairs(t) do
        if fn(v) then r[#r+1] = v end
    end
    return r
end

local function inspect_key(k)
    if type(k) == "string" and k:match("^[%a_][%w_]*$") then
        return k
    end
    return "[" .. table.inspect(k) .. "]"
end

local function sort_keys(a, b)
    local ta, tb = type(a), type(b)
    if ta == tb then
        return tostring(a) < tostring(b)
    end
    return ta < tb
end

local function inspect_value(value, seen, depth, max_depth)
    local tv = type(value)
    if tv == "string" then
        return string.format("%q", value)
    end
    if tv ~= "table" then
        return tostring(value)
    end

    local mt = getmetatable(value)
    if mt and mt.__tostring then
        return tostring(value)
    end

    if seen[value] then
        return "<cycle>"
    end
    if depth >= max_depth then
        return "{...}"
    end

    seen[value] = true

    local keys = {}
    local array_len = #value
    for k in pairs(value) do
        if type(k) ~= "number" or k < 1 or k > array_len or k % 1 ~= 0 then
            keys[#keys+1] = k
        end
    end
    table.sort(keys, sort_keys)

    local out = {}
    for i = 1, array_len do
        out[#out+1] = inspect_value(value[i], seen, depth + 1, max_depth)
    end
    for _, k in ipairs(keys) do
        out[#out+1] = inspect_key(k) .. " = " .. inspect_value(value[k], seen, depth + 1, max_depth)
    end

    seen[value] = nil

    if #out == 0 then
        return "{}"
    end

    local indent = string.rep("  ", depth)
    local child_indent = indent .. "  "
    return "{\n" .. child_indent .. table.concat(out, ",\n" .. child_indent) .. "\n" .. indent .. "}"
end

table.inspect = function(value, opts)
    opts = opts or {}
    return inspect_value(value, {}, 0, opts.max_depth or 6)
end

pprint = function(...)
    local out = {}
    for i = 1, select("#", ...) do
        out[#out+1] = table.inspect(select(i, ...))
    end
    print(table.concat(out, "\t"))
end

do
    local ffi = require("ffi")
    ffi.cdef([[typedef struct { uint8_t r, g, b, a; } hw_color_t;]])
    local _ct = ffi.typeof("hw_color_t")
    local function clamp(v, lo, hi)
        if v < lo then return lo end
        if v > hi then return hi end
        return v
    end
    local function byte(v)
        return math.floor(clamp(v, 0, 255) + 0.5)
    end
    local function float_byte(v)
        return math.floor(clamp(v, 0, 1) * 255 + 0.5)
    end
    ffi.metatype("hw_color_t", {
        __tostring = function(c)
            return string.format("%02x%02x%02x", c.r, c.g, c.b)
        end,
        __index = {
            hex = function(self)
                return string.format("%02x%02x%02x", self.r, self.g, self.b)
            end,
            with_alpha = function(self, a)
                return _ct(self.r, self.g, self.b, byte(a))
            end,
            with_alphaf = function(self, a)
                return _ct(self.r, self.g, self.b, float_byte(a))
            end,
        },
        __is_hw_color = true,
    })
    color._ct = _ct

    color.rgb = function(r, g, b)
        return _ct(byte(r), byte(g), byte(b), 255)
    end

    color.rgba = function(r, g, b, a)
        return _ct(byte(r), byte(g), byte(b), byte(a))
    end

    color.rgbf = function(r, g, b)
        return _ct(float_byte(r), float_byte(g), float_byte(b), 255)
    end

    color.rgbaf = function(r, g, b, a)
        return _ct(float_byte(r), float_byte(g), float_byte(b), float_byte(a))
    end

    color.hex = function(hex, alpha)
        if hex:sub(1, 1) == "#" then hex = hex:sub(2) end
        local v = tonumber(hex, 16)
        if not v or #hex ~= 6 then
            error("invalid color hex: " .. tostring(hex), 2)
        end
        local r = math.floor(v / 0x10000) % 256
        local g = math.floor(v / 0x100) % 256
        local b = v % 256
        local a = alpha and byte(alpha) or 255
        return _ct(r, g, b, a)
    end
end
