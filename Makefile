CC := clang

# Build Dir
BUILD_DIR := build
LINUXB_DIR := $(BUILD_DIR)/linux
WINB_DIR   := $(BUILD_DIR)/windows

SRC_DIR := src
INC_DIR := include
LIB_NAME := libcup
CUP_NAME := cup

# -fsanitize=memory -MMD -MP
CFLAGS := -ggdb -Wall -Wextra -O0 -I. -I./include -MMD -MP

# Linux flags
LINUXB_CFLAGS  := -D_GNU_SOURCE $(CFLAGS) -fsanitize=memory -fPIC
LINUXB_LDFLAGS := -ldl # -lm

# Windows 
WINB_TARGET    := x86_64-w64-mingw32
WINB_CFLAGS    := --target=$(WINB_TARGET) $(CFLAGS)
WINB_LDFLAGS   := --target=$(WINB_TARGET) -fuse-ld=lld

# Source files
SRC_FILES := \
	$(SRC_DIR)/lexer.c \
	$(SRC_DIR)/parser.c \
	$(SRC_DIR)/utils.c \
	$(SRC_DIR)/types.c \
	$(SRC_DIR)/moduler.c \
	$(SRC_DIR)/codegen/x64codegen.c \
	$(SRC_DIR)/resolver.c
#	$(SRC_DIR)/objwrite.c

# Object and Dependency
LINUXB_OBJS := $(patsubst %.c,$(LINUXB_DIR)/%.o,$(SRC_FILES))
WINB_SRC_FILES := $(SRC_FILES) dep/dlfcn.c
WINB_OBJS := $(patsubst %.c,$(WINB_DIR)/%.o,$(WINB_SRC_FILES))

DEP_FILES := $(LINUXB_OBJS:.o=.d) $(WINB_OBJS:.o=.d)

# Linux Targets
LINUXB_STATIC_LIB := $(LINUXB_DIR)/$(LIB_NAME).a
LINUXB_SHARED_LIB := $(LINUXB_DIR)/$(LIB_NAME).so
LINUXB_STATIC_EXE := $(LINUXB_DIR)/$(CUP_NAME)
LINUXB_SHARED_EXE := $(LINUXB_DIR)/$(CUP_NAME).elf

# Windows Targets
WINB_STATIC_LIB := $(WINB_DIR)/$(LIB_NAME).a
WINB_SHARED_LIB := $(WINB_DIR)/$(LIB_NAME).dll
WINB_IMPORT_LIB := $(WINB_DIR)/$(LIB_NAME).dll.a
WINB_STATIC_EXE := $(WINB_DIR)/$(CUP_NAME)_static.exe
WINB_SHARED_EXE := $(WINB_DIR)/$(CUP_NAME).exe

all: linux windows

linux: $(LINUXB_STATIC_LIB) $(LINUXB_SHARED_LIB) $(LINUXB_STATIC_EXE) $(LINUXB_SHARED_EXE)
windows: $(WINB_STATIC_LIB) $(WINB_SHARED_LIB) $(WINB_STATIC_EXE) $(WINB_SHARED_EXE)

-include $(DEP_FILES)

# ----------------- #
# LINUX BUILD RULES #
# ----------------- #
$(LINUXB_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(LINUXB_CFLAGS) -c $< -o $@

$(LINUXB_STATIC_LIB): $(LINUXB_OBJS)
	ar rcs $@ $^

$(LINUXB_SHARED_LIB): $(LINUXB_OBJS)
	$(CC) -shared -o $@ $^ $(LINUXB_LDFLAGS)

$(LINUXB_STATIC_EXE): main.c $(LINUXB_STATIC_LIB)
	$(CC) $(LINUXB_CFLAGS) $< $(LINUXB_STATIC_LIB) $(LINUXB_LDFLAGS) -o $@

$(LINUXB_SHARED_EXE): main.c $(SHARED_LIB)
	$(CC) $(LINUXB_CFLAGS) $< -L$(LINUXB_DIR) -lcup \
		-Wl,-rpath,'$$ORIGIN' \
		$(LINUXB_LDFLAGS) -o $@

# ------------------- #
# WINDOWS BUILD RULES #
# ------------------- #
$(WINB_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(WINB_CFLAGS) -DCUP_SHARED -DCUP_EXPORTS -c $< -o $@

$(WINB_STATIC_LIB): $(WINB_OBJS)
	llvm-ar rcs $@ $^

$(WINB_SHARED_LIB): $(WINB_OBJS)
	$(CC) $(WINB_LDFLAGS) -shared -o $@ $^ -Wl,--out-implib,$(WINB_IMPORT_LIB)

$(WINB_STATIC_EXE): main.c $(WINB_STATIC_LIB)
	$(CC) $(WINB_CFLAGS) $(WINB_LDFLAGS) $< $(WINB_STATIC_LIB) -o $@

$(WINB_SHARED_EXE): main.c $(SHARED_LIB)
	$(CC) $(WINB_CFLAGS) -DCUP_SHARED $(WINB_LDFLAGS) $< -L$(WINB_DIR) -lcup -o $@

# RUN & UTINITY RULES
run-linux: $(LINUXB_STATIC_EXE)
	./$(LINUXB_STATIC_EXE)

run-win: $(WINB_STATIC_EXE)
	wine ./$(WINB_STATIC_EXE)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all linux windows run-linux run-win clean