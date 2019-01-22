.POSIX:
.SUFFIXES: .glsl .spv

LDFLAGS = -lvulkan -lSDL2 -lm
CFLAGS = -std=c99 -Wall -Werror -D_POSIX_C_SOURCE=199309L

TRI_OBJ = triangle/triangle.o
TRI_SHD = triangle/shader.vert.spv triangle/shader.frag.spv

.glsl.spv:
	glslangValidator -V $< -o $@

tri: ${TRI_OBJ} ${TRI_SHD}
	${CC} ${LDFLAGS} ${TRI_OBJ} -o $@

clean:
	rm -f ${TRI_OBJ} ${TRI_SHD} tri
