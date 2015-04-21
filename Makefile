avrcore: main.cpp
	g++ -g $< -o avrcore -std=c++11 -DPROFILE -DATMEGA32U4

android:
	~/android-ndk-r10d/ndk-build

asm.js: main.cpp
	emcc -DATMEGA32U4 -O3 -s ASM_JS=1 $< -o avrcore.js -s EXPORTED_FUNCTIONS="['_main']"

emcc_avrcore.js: main.cpp
	emcc -DATMEGA32U4 -DLIBRARY -O3 -s ASM_JS=1 $< -o $@ -s EXPORTED_FUNCTIONS="['_loadPartialProgram','_engineInit','_fetchN']"

clean:
	-@rm avrcore
	-@rm avrcore.js
	-@rm emcc_avrcore.js
	-@rm -rf libs
	-@rm -rf obj
