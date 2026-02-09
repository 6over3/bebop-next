cmake_minimum_required(VERSION 3.16)
project(lua C)

set(LUA_CORE_SRC
    lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c
    ldblib.c ldebug.c ldo.c ldump.c lfunc.c lgc.c linit.c
    liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c
    lopcodes.c loslib.c lparser.c lstate.c lstring.c lstrlib.c
    ltable.c ltablib.c ltm.c lundump.c lutf8lib.c lvm.c lzio.c
)

add_library(lua OBJECT ${LUA_CORE_SRC})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(UNIX AND NOT APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PUBLIC m dl)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
endif()
