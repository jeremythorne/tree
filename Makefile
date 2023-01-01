CFLAGS= -g \
	-I../cglm/include \
	-I../ctl \
	-I../sokol \
	#-fsanitize=address

LDLIBS=-lGLESv2 -lglfw3 -lm -ldl -lpthread -lX11 #-lasan

tree: renderer.o mymath.o
