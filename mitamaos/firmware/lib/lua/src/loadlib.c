/* Minimal loadlib stub for ESP32 - no dynamic loading */
#define loadlib_c
#define LUA_LIB
#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int ll_require(lua_State *L) {
  luaL_error(L, "require not supported on ESP32");
  return 0;
}

static const luaL_Reg pk_funcs[] = {
  {NULL, NULL}
};

static const luaL_Reg ll_funcs[] = {
  {"require", ll_require},
  {NULL, NULL}
};

LUAMOD_API int luaopen_package(lua_State *L) {
  luaL_newlib(L, pk_funcs);
  return 1;
}
