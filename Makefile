CFLAGS= -g \
	-std=c11 \
	-I../cglm/include \
	-I../ctl \
	#-fsanitize=address

LDLIBS=-lGLESv2 -lglfw3 -lm -ldl -lpthread -lX11 #-lasan

tree: mymath.o
