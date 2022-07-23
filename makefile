all: cx-dock

cx-dock: cx_dock.c
	gcc -o cx-dock \
		-O3 \
		-lm \
		$(shell pkg-config --cflags --libs x11 gl glu glut xrender xrandr) \
		cx_dock.c

run: cx-dock
	./cx-dock

