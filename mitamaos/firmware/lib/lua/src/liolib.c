/* Minimal IO lib stub for ESP32 - print goes to Serial */
#define liolib_c
#define LUA_LIB
#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>

static int io_write(lua_State *L) {
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    size_t l;
    const char *s = luaL_tolstring(L, i, &l);
    printf("%s", s);
    lua_pop(L, 1);
  }
  return 0;
}
static int io_read(lua_State *L) {
  lua_pushnil(L);
  return 1;
}
static const luaL_Reg iolib[] = {
  {"write", io_write},
  {"read",  io_read},
  {NULL, NULL}
};
LUAMOD_API int luaopen_io(lua_State *L) {
  luaL_newlib(L, iolib);
  return 1;
}
