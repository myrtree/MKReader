CC := gcc

INCLUDES := -I/include/SDL -I/mingw/include -I../tstl2cl/include

CFLAGS += $(INCLUDES) $(shell sdl-config --cflags) $(shell agar-config --cflags)
LDFLAGS += $(shell sdl-config --libs) $(shell agar-config --libs) -lSDL_ttf ../tstl2cl/libtstl2cl.a

SOURCES  := app.c def_code_page.c mini_file_dlg.c color_widget.c

all: 
	$(CC) $(SOURCES) -o app.exe $(CFLAGS) $(LDFLAGS)

clean:
	rm -f *.o *~
