CC = clang
CFLAGS = -ggdb -Wall -Wextra -fsanitize=memory -O0 -I./ -I./include
LDFLAGS = -lm
LIB_NAME = libcup
CUP_NAME = cup
# -fsanitize=address 

# Source files
SRC_DIR   = src
SRC_FILES = $(SRC_DIR)/lexer.c \
            $(SRC_DIR)/parser.c \
            $(SRC_DIR)/utils.c \
            $(SRC_DIR)/types.c \
            $(SRC_DIR)/codegen/x64codegen.c

OBJ_FILES = $(SRC_FILES:.c=.o)

all: $(LIB_NAME).so $(LIB_NAME).a $(CUP_NAME).elf $(CUP_NAME)

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(LIB_NAME).a: $(OBJ_FILES)
	ar rcs $@ $^

$(LIB_NAME).so: $(OBJ_FILES)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(CUP_NAME).elf: ./main.c $(LIB_NAME).so $(LIB_NAME).a
	$(CC) $(CFLAGS) $< -L. -lcup $(LDFLAGS) -Wl,-rpath,'$$ORIGIN' -o $@

$(CUP_NAME): ./main.c $(LIB_NAME).so $(LIB_NAME).a
	$(CC) $(CFLAGS) $< $(LIB_NAME).a $(LDFLAGS) -o $@

# $(LIB_NAME).a: $(LIB_NAME).o
#		ar rcs $@ $^
#
# $(LIB_NAME).so: $(LIB_NAME).o
#		$(CXX) -shared -o $@ $^ $(LDFLAGS)
#
# $(LIB_NAME).o: ./libcup.c
#		$(CXX) $(CFLAGS) -fPIC -c $< -o $@
#
# $(CUP_NAME).elf: ./main.cpp $(LIB_NAME).so $(LIB_NAME).a
#		$(CXX) $(CFLAGS) ./main.cpp -L. -lcup $(LDFLAGS) -Wl,-rpath,'$$ORIGIN' -o $@
#
# $(CUP_NAME): ./main.cpp $(LIB_NAME).so $(LIB_NAME).a
#		$(CXX) $(CFLAGS) ./main.cpp $(LIB_NAME).a $(LDFLAGS) -o $@
#
# Run test (now works without LD_LIBRARY_PATH)
run: $(CUP_NAME)
		./$(CUP_NAME)

debug: $(CUP_NAME)
		lldb ./$(CUP_NAME)

ldd: $(CUP_NAME)
		ldd ./$(CUP_NAME)

clean:
	rm -f $(OBJ_FILES) $(LIB_NAME).so $(LIB_NAME).a $(CUP_NAME) $(CUP_NAME).elf

.PHONY: all run debug ldd clean
