ROOT_DIR= .
OBJ_DIR= $(ROOT_DIR)/obj
OUT_DIR= $(ROOT_DIR)/out
INSTALL_DIR= /usr/bin

SRC_DIR= $(ROOT_DIR)/src
INC_DIR= $(ROOT_DIR)/include

CXX= g++
CXXEXT= cpp
CXXFLAGS= -c -I$(INC_DIR)

LD= g++
LDEXT= o
LDFLAGS=

SRC_FILES= $(shell find $(SRC_DIR) -type f -name *.$(CXXEXT))
OBJ_FILES= $(patsubst $(SRC_DIR)/%.$(CXXEXT), $(OBJ_DIR)/%.$(LDEXT), $(SRC_FILES))

OUT_NAME= download
OUT_FILE= $(OUT_DIR)/$(OUT_NAME)

.PHONY: all clean cleanobj rebuild install uninstall run

all: $(OUT_FILE)

$(OUT_FILE): $(OBJ_FILES)
	mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS) $(OBJ_FILES) -o $(OUT_FILE)

$(OBJ_FILES): $(OBJ_DIR)/%.$(LDEXT): $(SRC_DIR)/%.$(CXXEXT)
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

clean: cleanobj
	rm -r -f $(OUT_DIR)

cleanobj:
	rm -r -f $(OBJ_DIR)

rebuild: clean all

run: all
	@$(OUT_FILE)

install: all
	cp $(OUT_FILE) $(INSTALL_DIR)/$(OUT_NAME)

uninstall:
	rm -f $(INSTALL_DIR)/$(OUT_NAME)