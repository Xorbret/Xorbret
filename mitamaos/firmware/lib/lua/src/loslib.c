/* Minimal OS lib stub for ESP32 */
#define loslib_c
#define LUA_LIB
#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

extern unsigned long millis(void);

static int os_clock(lua_State *L) {
  lua_pushnumber(L, (lua_Number)millis() / 1000.0);
  return 1;
}
static int os_time(lua_State *L) {
  lua_pushinteger(L, (lua_Integer)(millis() / 1000));
  return 1;
}
static const luaL_Reg syslib[] = {
  {"clock", os_clock},
  {"time",  os_time},
  {NULL, NULL}
};
LUAMOD_API int luaopen_os(lua_State *L) {
  luaL_newlib(L, syslib);
  return 1;
}
