CFLAGS += -Wall -Wextra -Wshadow
LDLIBS += -lm -lSDL -lSDL_image

.PHONY: all
all: engine

.PHONY: test
test: all
	./engine

.PHONY: clean
clean:
	$(RM) engine