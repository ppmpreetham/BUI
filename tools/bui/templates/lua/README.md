# {{title}}

A [BUI](https://bui.preetham.top) app in Lua 5.4.

```sh
bui dev            # run it (lua main.lua, with this project's rocks)
bui doctor         # check the setup, and fix what is wrong
bui update         # update bui and this project's blenderui
```

Rocks are installed into `lua_modules/` (nothing global): `luarocks --tree lua_modules install <rock>`.
Lint with `luarocks --tree lua_modules path` set and `luacheck .`; format with [StyLua](https://github.com/JohnnyMorganz/StyLua).
