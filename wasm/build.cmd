@echo off
rem Builds build\wasm\soundmonreplay.wasm with Emscripten. Run it from the repository's root, with em++ on the path
rem (emsdk_env.bat sets that up). The result is a single file that needs no JavaScript from Emscripten.
setlocal
if not exist build\wasm mkdir build\wasm
call em++ -O3 -std=c++17 -Iinclude -Isrc ^
	src/module.cpp src/paula.cpp src/replayer.cpp src/soundmonreplay.cpp wasm/soundmonreplay_wasm.cpp ^
	--no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 ^
	-sEXPORTED_FUNCTIONS=_malloc,_free,_smon_version,_smon_wasm_open,_smon_wasm_configure,_smon_close,_smon_subsong_count,_smon_select_subsong,_smon_render_float,_smon_seek ^
	-o build/wasm/soundmonreplay.wasm || exit /b 1
echo Built build\wasm\soundmonreplay.wasm
