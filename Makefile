CC := gcc
CFLAGS := -ggdb -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter
CPPFLAGS :=
LDFLAGS :=
LDLIBS := -lws2_32 -lhttp_parser

SRC	:= \
	src/main.c \
	src/queue.c
TARGET := bin/server

.PHONY: all
all: $(TARGET)

$(TARGET): $(SRC:src/%.c=build/%.o)
	mkdir -p $(@D)
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

build/%.o: src/%.c
	mkdir -p $(@D)
	$(CC) -c $< -o $@ -MMD -MF $(@:.o=.d) $(CFLAGS) $(CPPFLAGS)

-include $(SRC:src/%.c=build/%.d)

.PHONY: run
run: all
	./$(TARGET)

.PHONY: clean
clean:
	rm -rf bin build
