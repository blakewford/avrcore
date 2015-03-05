avrcore: main.cpp
	g++ -g $< -o avrcore -std=c++11 -DPROFILE

android:
	~/android-ndk-r10d/ndk-build

asm.js: main.cpp
	emcc -O3 -s ASM_JS=1 $< -o avrcore.js

clean:
	-@rm avrcore
	-@rm avrcore.js
	-@rm -rf libs
	-@rm -rf obj
