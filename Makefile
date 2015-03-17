avrcore: main.cpp
	g++ -g $< -o avrcore -std=c++11 -DPROFILE

android:
	~/android-ndk-r10d/ndk-build

asm.js: main.cpp
	emcc -O3 -s ASM_JS=1 $< -o avrcore.js -s EXPORTED_FUNCTIONS="['_main','_loadProgram','_loadPartialProgram','_engineInit','_execProgram']"

emcc_avrcore.js: main.cpp
	emcc -DLIBRARY -O3 -s ASM_JS=1 $< -o $@ -s EXPORTED_FUNCTIONS="['_main','_loadProgram','_loadPartialProgram','_engineInit','_execProgram','_fetch']"

clean:
	-@rm avrcore
	-@rm avrcore.js
	-@rm emcc_avrcore.js
	-@rm -rf libs
	-@rm -rf obj
