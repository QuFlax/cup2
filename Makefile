CC := clang

BUILD_DIR := build
SRC_DIR := src

LIB_NAME := libcup
CUP_NAME := cup

CFLAGS := -ggdb -Wall -Wextra -fsanitize=memory -O0 -I. -I./include
LDFLAGS := -lm

SRC_FILES := \
	$(SRC_DIR)/lexer.c \
	$(SRC_DIR)/parser.c \
	$(SRC_DIR)/utils.c \
	$(SRC_DIR)/types.c \
	$(SRC_DIR)/moduler.c \
	$(SRC_DIR)/codegen/x64codegen.c

OBJ_FILES := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRC_FILES))

STATIC_LIB := $(BUILD_DIR)/$(LIB_NAME).a
SHARED_LIB := $(BUILD_DIR)/$(LIB_NAME).so
STATIC_EXE := $(BUILD_DIR)/$(CUP_NAME)
SHARED_EXE := $(BUILD_DIR)/$(CUP_NAME).elf

all: $(STATIC_LIB) $(SHARED_LIB) $(STATIC_EXE) $(SHARED_EXE)

# Create build directories automatically
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(STATIC_LIB): $(OBJ_FILES)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(SHARED_LIB): $(OBJ_FILES)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(STATIC_EXE): main.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) -o $@

$(SHARED_EXE): main.c $(SHARED_LIB)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lcup \
		-Wl,-rpath,'$$ORIGIN' \
		$(LDFLAGS) -o $@

run: $(STATIC_EXE)
	./$(STATIC_EXE)

run-shared: $(SHARED_EXE)
	./$(SHARED_EXE)

debug: $(STATIC_EXE)
	lldb ./$(STATIC_EXE)

ldd: $(SHARED_EXE)
	ldd ./$(SHARED_EXE)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run run-shared debug ldd clean