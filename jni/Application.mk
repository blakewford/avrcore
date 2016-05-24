APP_ABI := x86_64
APP_OPTIM := debug
APP_CPPFLAGS += -fPIE -std=c++11 -DPROFILE -DATMEGA32U4
APP_LDFLAGS += -Wl,-pie
APP_STL := gnustl_static
