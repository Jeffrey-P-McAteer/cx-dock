all: cx-dock

cx-dock: cx_dock.c
	gcc -o cx-dock \
		-lm \
		$(shell pkg-config --cflags --libs x11) \
		cx_dock.c

run: cx-dock
	./cx-dock

