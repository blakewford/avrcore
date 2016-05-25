avrcore: main.cpp
	g++ -Ofast $< -o $@ -std=c++11 -DPROFILE -DATMEGA32U4

gamebuino: main.cpp
	g++ -g $< -o $@ -std=c++11 -DPROFILE -DATMEGA328

android:
	~/android-ndk-r10e/ndk-build

asm.js: main.cpp
	emcc -DATMEGA32U4 -O3 -s ASM_JS=1 $< -o avrcore.js -s EXPORTED_FUNCTIONS="['_main']"

emcc_avrcore.js: main.cpp
	emcc -DATMEGA32U4 -DLIBRARY -O3 -s ASM_JS=1 $< -o $@ -s EXPORTED_FUNCTIONS="['_loadPartialProgram','_engineInit','_fetchN']"

gamebuino_avrcore.js: main.cpp
	emcc -DATMEGA328 -DLIBRARY -O3 -s ASM_JS=1 $< -o $@ -s EXPORTED_FUNCTIONS="['_loadPartialProgram','_engineInit','_fetchN']"

clean:
	-@rm avrcore
	-@rm gamebuino
	-@rm avrcore.js
	-@rm emcc_avrcore.js
	-@rm gamebuino_avrcore.js
	-@rm -rf libs
	-@rm -rf obj
