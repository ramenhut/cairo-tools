
CC = g++
src = $(wildcard cairo/*.cpp) \
	$(wildcard *.cpp)
	
obj = $(src:.cpp=.o)

CXXFLAGS = -O2 -DNDEBUG -w
LDFLAGS = -framework OpenGL -framework GLUT

player: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) player