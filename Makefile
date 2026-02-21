CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Isrc
LDFLAGS = -lm
SRCS    = src/lexer.c src/ast.c src/parser.c src/value.c \
          src/interpreter.c src/builtins.c src/module.c src/main.c
TARGET  = bin/interpreter

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS) | bin
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

bin:
	mkdir -p bin

clean:
	rm -rf bin

test: $(TARGET)
	@echo "=== Running basic test ==="
	@$(TARGET) tests/test_basic.txt && echo "PASS" || echo "FAIL"
	@echo "=== Running functions test ==="
	@$(TARGET) tests/test_functions.txt && echo "PASS" || echo "FAIL"
	@echo "=== Running patterns test ==="
	@$(TARGET) tests/test_patterns.txt && echo "PASS" || echo "FAIL"
