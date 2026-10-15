# Portable Windows build using project-local w64devkit + raylib

CXX := packages/w64devkit/bin/g++.exe
CC  := packages/w64devkit/bin/gcc.exe

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR := bin
RES_DIR := resources

RAYLIB_DIR := packages/raylib-6.0_win64_mingw-w64
RAYLIB_INC := $(RAYLIB_DIR)/include
RAYLIB_LIB := $(RAYLIB_DIR)/lib

TARGET := $(BIN_DIR)/game.exe

CPP_SOURCES := $(shell find $(SRC_DIR) -type f -name "*.cpp")
C_SOURCES   := $(shell find $(SRC_DIR) -type f -name "*.c")
CPP_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
C_OBJECTS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
OBJECTS := $(CPP_OBJECTS) $(C_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS := -I$(INC_DIR) -I$(SRC_DIR) -I$(INC_DIR)/ -I$(SRC_DIR)/ -isystem $(RAYLIB_INC)
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -MMD -MP
CFLAGS   := -std=c11 -Wall -Wextra -O2 -MMD -MP

LDFLAGS := -L$(RAYLIB_LIB)
LDLIBS := -lwininet -lraylib -lopengl32 -lgdi32 -lwinmm -static -static-libgcc -static-libstdc++

.PHONY: all clean run resources

all: $(TARGET) resources

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p "$(BIN_DIR)"

resources: | $(BIN_DIR)
	@cp -r "$(RES_DIR)" "$(BIN_DIR)/" 2>/dev/null || true

run: all
	cd "$(BIN_DIR)" && ./game.exe

clean:
	@rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"

-include $(DEPS)
