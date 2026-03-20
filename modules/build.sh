#!/bin/bash

LIBS=$(pkg-config --libs gobject-introspection-1.0 gtk4)
FLAGS=$(pkg-config --cflags gobject-introspection-1.0 gtk4)
#gcc -shared -fPIC $FLAGS -o liblox_hello.so liblox_hello.c -I../
#gcc -shared -fPIC $FLAGS -o liblox_gtk4.so liblox_gtk4.c -I../
gcc -shared -fPIC $FLAGS -o liblox_gi.so liblox_gi.c -I../
