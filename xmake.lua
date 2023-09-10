
--cflags 

for index, value in ipairs(modules) do
    mod=string.upper(value)..'_MODULE '
    add_defines(mod)
    -- add_ldflags(mod)
end

includes("*/xmake.lua")
includes("./libs/*/xmake.lua")