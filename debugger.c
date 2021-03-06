/******************************************************************************
* Copyright (C) 2011 Robert Ray<louirobert@gmail.com>.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/

#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <Windows.h>

/*
** Compile command:
** cl debugger.c /LD /MD /EHs /O2
*/
#pragma comment(lib,"lua5.1.lib")

static HANDLE g_hStdOut;
static WORD g_TxtAttr;

#define ChangeTextColor() SetConsoleTextAttribute(g_hStdOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define RestoreTextColor() SetConsoleTextAttribute(g_hStdOut, g_TxtAttr)

static const luaL_Reg entries[] = { 0 };

static void hook(lua_State *L, lua_Debug *ar);

enum CMD
{
    STEP = 1,
    OVER,
    FINISH,
    RUN
};

__declspec(dllexport) int luaopen_robert_debugger(lua_State * L)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    g_hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(g_hStdOut, &bi);
    g_TxtAttr = bi.wAttributes;

    lua_pushliteral(L, "debugger");
    lua_newtable(L);
    lua_pushliteral(L, "breakpoints");
    lua_newtable(L);
    lua_rawset(L, -3);
    lua_pushliteral(L, "cmd");
    lua_pushinteger(L, STEP);
    lua_rawset(L, -3);
    lua_pushliteral(L, "stacklevel");
    lua_pushinteger(L, 0);
    lua_rawset(L, -3);
    lua_rawset(L, LUA_REGISTRYINDEX);

    luaL_register(L, "robert.debugger", entries);
    lua_sethook(L, hook, LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET, 0);
    return 1;
}

static void prompt(lua_State *L, lua_Debug * ar);
static void checkBreakPoint(lua_State *L, lua_Debug * ar);

void hook(lua_State * L, lua_Debug * ar)
{
    int event = ar->event;
    int top = lua_gettop(L);

    lua_pushliteral(L, "debugger");
    lua_rawget(L, LUA_REGISTRYINDEX);

    if (event == LUA_HOOKLINE) {
        int cmd;

        lua_pushliteral(L, "cmd");
        lua_rawget(L, -2);
        cmd = lua_tointeger(L, -1);
        lua_pop(L, 1);

        if (cmd == STEP) {
            lua_pushliteral(L, "stacklevel");
            lua_pushinteger(L, 0);
            lua_rawset(L, -3);
            prompt(L, ar);
        }
        else if (cmd == OVER) {
            int level;

            lua_pushliteral(L, "stacklevel");
            lua_rawget(L, -2);
            level = lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (!level)
                prompt(L, ar);
            else
                checkBreakPoint(L, ar);
        }
        else if (cmd == FINISH) {
            //prompt(L, ar);
        }
        else if (cmd == RUN) {
            checkBreakPoint(L, ar);
        }
    }
    else {
        int level;
        assert(event != LUA_HOOKCOUNT);

        lua_pushliteral(L, "stacklevel");
        lua_rawget(L, -2);
        level = lua_tointeger(L, -1);
        lua_pop(L, 1);

        if (event == LUA_HOOKCALL) {
            level++;
        }
        else if (event == LUA_HOOKRET || event == LUA_HOOKTAILRET) {
            if (level)
                level--;
        }
        lua_pushliteral(L, "stacklevel");
        lua_pushinteger(L, level);
        lua_rawset(L, -3);
    }
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

/*
** Check if the current line contains a breakpoint. If yes, break and prompt
** for user, and reset statck level to 0 preparing for the next "OVER" command.
** On top of L is the "debugger" table stored in LUA_REGISTRYINDEX. L stays
** unchanged after call, but the "debugger" table may be changed.
*/
void checkBreakPoint(lua_State *L, lua_Debug * ar)
{
    int breakpoint;
    char path[_MAX_PATH + 1];

    lua_getinfo(L, "Sl", ar);
    _fullpath(path, ar->short_src, _MAX_PATH);
#ifndef PATH_CASE_SENSITIVE
    _strlwr(path);
#endif

    lua_pushliteral(L, "breakpoints");
    lua_rawget(L, -2);
    lua_pushstring(L, path);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, ar->currentline);
        breakpoint = lua_isnil(L, -1) ? 0 : 1;
        lua_pop(L, 1);
    }
    else
        breakpoint = 0;
    lua_pop(L, 2);

    if (breakpoint) {
        lua_pushliteral(L, "stacklevel");
        lua_pushinteger(L, 0);
        lua_rawset(L, -3);
        prompt(L, ar);
    }
}

static char * parseOneArg(char * begin, char * end, char ** endPtr);
static void watch(lua_State * L, lua_Debug * ar, char * argBegin, char * argEnd);
static void exec(lua_State * L, lua_Debug * ar, char * argBegin, char * argEnd);
static void listLocals(lua_State * L, lua_Debug * ar, char * argBegin, char * argEnd);
static void listUpVars(lua_State * L, lua_Debug * ar, char * argBegin, char * argEnd);
static void printStack(lua_State * L);
static void setBreakPoint(lua_State * L, const char * src, char * argBegin,
    char * argEnd, int del);
static void listBreakPoints(lua_State * L);
static void showHelp();

#define CMD_LINE 1024

/*
** On top of L is the "debugger" table stored in LUA_REGISTRYINDEX. L stays
** unchanged after call, but the "debugger" table may be changed.
*/
void prompt(lua_State * L, lua_Debug * ar)
{
    int cmd;
    int top = lua_gettop(L);

    ChangeTextColor();

    lua_getinfo(L, "nSl", ar);
    printf("%s \tLine:%d \tName:%s \tWhat:%s\n", ar->short_src, ar->currentline,
        ar->name ? ar->name : "(N/A)", *ar->what ? ar->what : "(N/A)");

    while (1) {
        char buf[CMD_LINE];
        char * end;
        char * p;
        char * pCmd;

        printf("?>");
        fgets(buf, CMD_LINE, stdin);
        end = buf + strlen(buf);
        pCmd = parseOneArg(buf, end, &p);

        if (!pCmd) { //an empty line
            printf("Invalid command!\n");
            continue;
        }
        p++;

        if (!_stricmp(pCmd, "s") || !_stricmp(pCmd, "step")) {
            cmd = STEP;
            break;
        }
        if (!_stricmp(pCmd, "o") || !_stricmp(pCmd, "Over")) {
            cmd = OVER;
            break;
        }
        if (!_stricmp(pCmd, "f") || !_stricmp(pCmd, "Finish")) {
            cmd = FINISH;
            break;
        }
        else if (!_stricmp(pCmd, "r") || !_stricmp(pCmd, "run")) {
            cmd = RUN;
            lua_pushliteral(L, "breakpoints");
            lua_rawget(L, -2);
            lua_pushnil(L);
            if (!lua_next(L, -2)) { //When no breakpoints exists, disable the hook.
                lua_sethook(L, hook, 0, 0);
                lua_pop(L, 1);
            }
            else
                lua_pop(L, 3);
            break;
        }
        else if (!_stricmp(pCmd, "ll") || !_stricmp(pCmd, "listLocals")) {
            listLocals(L, ar, p, end);
        }
        else if (!_stricmp(pCmd, "lu") || !_stricmp(pCmd, "listUpVars")) {
            listUpVars(L, ar, p, end);
        }
        else if (!_stricmp(pCmd, "ps") || !_stricmp(pCmd, "printStack")) {
            printStack(L);
        }
        else if (!_stricmp(pCmd, "w") || !_stricmp(pCmd, "watch")) {
            watch(L, ar, p, end);
        }
        else if (!_stricmp(pCmd, "e") || !_stricmp(pCmd, "exec")) {
            exec(L, ar, p, end);
        }
        else if (!_stricmp(pCmd, "sb") || !_stricmp(pCmd, "setBreakPoint")) {
            setBreakPoint(L, ar->short_src, p, end, 0);
        }
        else if (!_stricmp(pCmd, "db") || !_stricmp(pCmd, "delBreakPoint")) {
            setBreakPoint(L, ar->short_src, p, end, 1);
        }
        else if (!_stricmp(pCmd, "lb") || !_stricmp(pCmd, "listBreakPoints")) {
            listBreakPoints(L);
        }
        else if (!_stricmp(pCmd, "h") || !_stricmp(pCmd, "help")) {
            showHelp();
        }
        else {
            printf("Invalid command! Type 'help' or 'h' for help.\n");
        }
    }

    lua_pushliteral(L, "cmd");
    lua_pushinteger(L, cmd);
    lua_rawset(L, -3);
    assert(top == lua_gettop(L));

    RestoreTextColor();
}

char * parseOneArg(char * begin, char * end, char ** endPtr)
{
    char * p;
    char * pArg;
    for (p = begin; p != end && isspace(*p); ++p);
    if (p == end)
        return NULL;

    pArg = p;
    for (; p!= end && !isspace(*p); ++p);
    *p = 0;
    if (endPtr)
        *endPtr = p;
    return pArg;
}

static int sortKey(lua_State * L);

/*
** A key-value pair is on top of L. L stays unchanged after call.
*/
static void printTabPair(lua_State * L, int leadingSp)
{
    int keytype = lua_type(L, -2);
    int valtype = lua_type(L, -1);

    switch(keytype) {
        case LUA_TSTRING: {
            printf("%*s* KT(string) \tKey(%s) \t", leadingSp, "", lua_tostring(L, -2));
            break;
        }
        case LUA_TNUMBER: {
            printf("%*s* KT(number) \tKey(%.8f) \t", leadingSp, "", lua_tonumber(L, -2));
            break;
        }
        case LUA_TTABLE: {
            printf("%*s* KT(table) \tKey(%08X) \t", leadingSp, "", lua_topointer(L, -2));
            break;
        }
        case LUA_TFUNCTION: {
            printf("%*s* KT(function) \tKey(%08X) \t", leadingSp, "", lua_topointer(L, -2));
            break;
        }
        case LUA_TUSERDATA: {
            printf("%*s* KT(userdata) \tKey(%08X) \t", leadingSp, "", lua_topointer(L, -2));
            break;
        }
        case LUA_TLIGHTUSERDATA: {
            printf("%*s* KT(light userdata) \tKey(%08X) \t", leadingSp, "", lua_topointer(L, -2));
            break;
        }
        case LUA_TBOOLEAN: {
            printf("%*s* KT(boolean) \tKey(%s) \t", leadingSp, "", lua_toboolean(L, -2) ? "true" : "false");
            break;
        }
        case LUA_TTHREAD: {
            printf("%*s* KT(thread) \tKey(%08X) \t", leadingSp, "", lua_topointer(L, -2));
            break;
        }
        case LUA_TNIL: {
            printf("%*s* KT(nil) \tKey(nil) \t", leadingSp, "");
            break;
        }
    }

    switch(valtype) {
        case LUA_TSTRING: {
            printf("VT(string) \tVal(%s)\n", lua_tostring(L, -1));
            break;
        }
        case LUA_TNUMBER: {
            printf("VT(number) \tVal(%.8f)\n", lua_tonumber(L, -1));
            break;
        }
        case LUA_TTABLE: {
            printf("VT(table) \tVal(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TFUNCTION: {
            printf("VT(function) \tVal(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TUSERDATA: {
            printf("VT(userdata) \tVal(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TLIGHTUSERDATA: {
            printf("VT(light userdata) \tVal(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TBOOLEAN: {
            printf("VT(boolean) \tVal(%s)\n", lua_toboolean(L, -1) ? "true" : "false");
            break;
        }
        case LUA_TTHREAD: {
            printf("VT(thread) \tVal(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TNIL: {
            printf("VT(nil) \tVal(nil)\n");
            break;
        }
    }
}

/*
** A table is on top of L. L stays unchanged after call.
*/
static void expandTable(lua_State * L, int level, int leadingSp)
{
    int n;
    int i;
    if (!level)
        return;

    n = sortKey(L);
    for (i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        lua_pushvalue(L, -1);
        lua_gettable(L, -4);
        printTabPair(L, leadingSp);
        if (lua_istable(L, -1)) {
            expandTable(L, level - 1, leadingSp + 2);
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
}

/*
** Variable value is on top of L. L stays unchanged after call.
*/
static void printVar(const char * name, lua_State * L, const char * scope, int tabLevel)
{
    int type;
    type = lua_type(L, -1);

    if (scope)
        printf("Scope(%s) \t", scope);
    printf("Name(%s) \t", name);

    switch(type) {
        case LUA_TSTRING: {
            printf("Type(string) \tValue(%s)\n", lua_tostring(L, -1));
            break;
        }
        case LUA_TNUMBER: {
            printf("Type(number) \tValue(%.8f)\n", lua_tonumber(L, -1));
            break;
        }
        case LUA_TTABLE: {
            printf("Type(table) \tValue(%08X)\n", lua_topointer(L, -1));
            if (tabLevel > 0) {
                expandTable(L, tabLevel, 2);
            }
            break;
        }
        case LUA_TFUNCTION: {
            printf("Type(function) \tValue(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TUSERDATA: {
            printf("Type(userdata) \tValue(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TLIGHTUSERDATA: {
            printf("Type(light userdata) \tValue(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TBOOLEAN: {
            printf("Type(boolean) \tValue(%s)\n", lua_toboolean(L, -1) ? "true" : "false");
            break;
        }
        case LUA_TTHREAD: {
            printf("Type(thread) \tValue(%08X)\n", lua_topointer(L, -1));
            break;
        }
        case LUA_TNIL: {
            printf("Type(nil) \tValue(nil)\n");
            break;
        }
    }
}

void listLocals(lua_State * L, lua_Debug * ar, char * p, char * end)
{
    struct lua_Debug AR;
    int level = -1;
    int i = 1;
    const char * name;

    if (p < end && (p = parseOneArg(p, end, NULL))) {
        level = strtol(p, NULL, 10);
    }
    if (level < 1)
        level = 1;
    if (--level > 0) {
        if (!lua_getstack(L, level, &AR)) {
            printf("No local variable info available at stack level %d.\n", level + 1);
            return;
        }
        ar = &AR;
    }

    printf("Local Variables of Stack Level %d:>>>>>>>>\n", level + 1);
    while ((name = lua_getlocal(L, ar, i++))) {
        if (strcmp(name, "(*temporary)"))
            printVar(name, L, NULL, 0);
        lua_pop(L, 1);
    }
    printf("<<<<<<<<\n");
}

void listUpVars(lua_State * L, lua_Debug * ar, char * p, char * end)
{
    struct lua_Debug AR;
    int level = -1;
    int i = 1;
    const char * name;

    if (p < end && (p = parseOneArg(p, end, NULL))) {
        level = strtol(p, NULL, 10);
    }
    if (level < 1)
        level = 1;
    if (--level > 0) {
        if (!lua_getstack(L, level, &AR)) {
            printf("No up-variable info available at stack level %d.\n", level + 1);
            return;
        }
        ar = &AR;
    }

    if (lua_getinfo(L, "f", ar)) {
        printf("Up-Variables of Stack Level %d:>>>>>>>>\n", level + 1);
        while ((name = lua_getupvalue(L, -1, i++))) {
            printVar(name, L, NULL, 0);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        printf("<<<<<<<<\n");
    }
}

void printStack(lua_State * L)
{
    struct lua_Debug ar;
    int i = 0;
    printf("Call Stack:>>>>>>>>\n");
    while (lua_getstack(L, i, &ar)) {
        lua_getinfo(L, "nSl", &ar);
        printf("%s \tLine:%d \tName:%s \tWhat:%s\n", ar.short_src, ar.currentline,
            ar.name ? ar.name : "(N/A)", *ar.what ? ar.what : "(N/A)");
        i++;
    }
    printf("<<<<<<<<\n");
}

/*
** L stays unchanged after call.
*/
void watch(lua_State * L, lua_Debug * ar, char * p, char * end)
{
    char * name;
    int tabLevel = 0;   //used only when the value being watched is a table
    int i = 1;

    if (p >= end || !(name = parseOneArg(p, end, &p))) {
        printf("Invalid argument!\n");
        return;
    }
    if (++p < end && (p = parseOneArg(p, end, NULL))) {
        tabLevel = strtol(p, NULL, 10);
        if (tabLevel < 0)
            tabLevel = 0;
    }

    //check if it's a local var
    lua_pushnil(L);
    while ((p = (char *)lua_getlocal(L, ar, i++))) {
        if (!strcmp(name, p))
            lua_replace(L, -2);
        else
            lua_pop(L, 1);
    }
    if (!lua_isnil(L, -1)) {
        printVar(name, L, "local", tabLevel);
        lua_pop(L, 1);
        return;
    }

    //check if it's an up-var
    lua_getinfo(L, "f", ar);
    i = 1;
    while ((p = (char *)lua_getupvalue(L, -1, i++))) {
        if (!strcmp(name, p))
            lua_replace(L, -3);
        else
            lua_pop(L, 1);
    }
    if (!lua_isnil(L, -2)) {
        lua_pop(L, 1);
        printVar(name, L, "up", tabLevel);
        lua_pop(L, 1);
        return;
    }
    lua_remove(L, -2);

    //check if it's a global
    lua_getfenv(L, -1);
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) {
        printVar(name, L, "global", tabLevel);
        lua_pop(L, 3);
        return;
    }

    lua_pop(L, 3);
    printf("Variable(%s) is not defined!\n", name);
}

/*
** L stays unchanged after call.
*/
void exec(lua_State * L, lua_Debug * ar, char * p, char * end)
{
    if (p >= end) {
        printf("Invalid argument!\n");
        return;
    }
    if (luaL_loadstring(L, p)) {
        printf("%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    if (lua_pcall(L, 0, 0, 0)) {
        printf("%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

/*
** On top of L is the "debugger" table stored in LUA_REGISTRYINDEX. L stays
** unchanged after call, but the "debugger" table may be changed.
*/
void setBreakPoint(lua_State * L, const char * src, char * p, char * end, int del)
{
    int line;
    char * pFile;
    char * pLine;
    char path[_MAX_PATH + 1];

    if (p >= end || !(pFile = parseOneArg(p, end, &p))
        || ++p >= end || !(pLine = parseOneArg(p, end, &p))
        || (line = strtol(pLine, NULL, 10)) <= 0) {
        printf("Invalid argument!\n");
        return;
    }

    if (!strcmp(pFile, ".")) {
        pFile = (char *)src;
    }
    if (!_fullpath(path, pFile, _MAX_PATH) || _access(path, 0)) {
        printf("Invalid path!\n");
        return;
    }
#ifndef PATH_CASE_SENSITIVE
    _strlwr(path);
#endif

    lua_pushliteral(L, "breakpoints");
    lua_rawget(L, -2);
    lua_pushstring(L, path);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushstring(L, path);
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
    }
    if (del)
        lua_pushnil(L);
    else
        lua_pushboolean(L, 1);
    lua_rawseti(L, -2, line);

    if (del) { //check if the path table is empty
        lua_pushnil(L);
        if (!lua_next(L, -2)) { //remove the entry from breakpoints table if it's empty
            lua_pushstring(L, path);
            lua_pushnil(L);
            lua_rawset(L, -4);
        }
        else
            lua_pop(L, 2);
    }
    lua_pop(L, 2);
}

/*
** Given a table on top of L, it sorts the keys of that table and stores
** the sorted keys in a new table returned on top of L. Thus L increases by 1.
** Return the length of the new table.
*/
int sortKey(lua_State * L)
{
    int i = 1;
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, -3)) {
        lua_pushvalue(L, -2);
        lua_rawseti(L, -4, i++);
        lua_pop(L, 1);
    }

    lua_getglobal(L, "table");
    lua_getfield(L, -1, "sort");
    lua_pushvalue(L, -3);
    lua_call(L, 1, 0);
    lua_pop(L, 1);
    return i - 1;
}

/*
** On top of L is the "debugger" table stored in LUA_REGISTRYINDEX. L stays
** unchanged after call.
*/
void listBreakPoints(lua_State * L)
{
    int i, n;
    int top = lua_gettop(L);

    lua_pushliteral(L, "breakpoints");
    lua_rawget(L, -2);
    n = sortKey(L);

    printf("Break Points:>>>>>>>>\n");
    for (i = 1; i <= n; i++) {
        int j, m;
        const char * path;

        lua_rawgeti(L, -1, i);
        path = lua_tostring(L, -1);
        lua_rawget(L, -3);
        assert(lua_istable(L, -1));

        m = sortKey(L);
        for (j = 1; j <= m; j++) {
            lua_rawgeti(L, -1, j);
            printf("File %s Line %d\n", path, lua_tointeger(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 2);
    printf("<<<<<<<<\n");
    assert(top == lua_gettop(L));
}

#define TIPS \
"Lua Debugger by Robert Ray<louirobert@gmail.com> @2011 Version 1.0.1\n"\
"Commands:\n"\
"'step' or 's': Step into a statement.\n"\
"'over' or 'o': Step over a statement.\n"\
"'run' or 'r': Run until hit a breakpoint.\n"\
"'setBreakPoint' or 'sb' <file> <line>: Set a breakpoint in file.\n"\
"'delBreakPoint' or 'db' <file> <line>: Delete a breakpoint in file.\n"\
"'listBreakPoints' or 'lb': List all breakpoints.\n"\
"'watch' or 'w' <var-name> [table level]: Watch a single variable from the perspective of the top level call stack."\
"If the variable is a table, then an optional argument(table level) specifies how many levels the table is expanded.\n"\
"'listLocals' or 'll' [stack level]: List all local variables of a stack level. Default stack level is 1.\n"\
"'listUpVars' or 'lu' [stack level]: List all up-variables of a stack level. Default stack level is 1.\n"\
"'printStack' or 'ps': Print call stack.\n"\
"'exec' or 'e' <script>: Execute script in the context of the debuggee. "\
"This may have side effects on the debuggee.\n"\
"'help' or 'h': Show this help."

void showHelp()
{
    puts(TIPS);
}
