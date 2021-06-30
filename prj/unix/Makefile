ROOT_DIR= ../..
OBJ_DIR= $(ROOT_DIR)/obj
OUT_DIR= $(ROOT_DIR)/out
INSTALL_DIR= /usr/bin
EXTERN_DIR= $(ROOT_DIR)/extern
CURL_DIR= $(EXTERN_DIR)/curl
EXTERN_OUT_DIR= $(ROOT_DIR)/externout
CURL_INSTALL_DIR= $(PWD)/$(EXTERN_OUT_DIR)/curl
CURL_INC_DIR= $(CURL_INSTALL_DIR)/include
CURL_LIB_DIR= $(CURL_INSTALL_DIR)/lib
FULL_ROOT_DIR= $(PWD)

SRC_DIR= $(ROOT_DIR)/src
INC_DIR= $(ROOT_DIR)/include

MAKE= make
CXX= g++
CXXEXT= cpp
CXXFLAGS= -c -I$(INC_DIR) -I$(CURL_INC_DIR)

LD= g++
LDEXT= o
LDFLAGS= -L$(CURL_LIB_DIR)
LDLIBS= -lcurl

SRC_FILES= $(shell find $(SRC_DIR) -type f -name *.$(CXXEXT))
OBJ_FILES= $(patsubst $(SRC_DIR)/%.$(CXXEXT), $(OBJ_DIR)/%.$(LDEXT), $(SRC_FILES))

OUT_NAME= download
OUT_FILE= $(OUT_DIR)/$(OUT_NAME)

.PHONY: all clean cleanobj rebuild install uninstall run buildcurl cleancurl

all: $(OUT_FILE)

$(OUT_FILE): $(OBJ_FILES)
	mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS) $(OBJ_FILES) -o $(OUT_FILE) $(LDLIBS)

$(OBJ_FILES): buildcurl
$(OBJ_DIR)/%.$(LDEXT): $(SRC_DIR)/%.$(CXXEXT)
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

clean: cleanobj cleancurl
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

buildcurl: $(CURL_DIR)/Makefile
	cd $(FULL_ROOT_DIR) && \
	cd $(CURL_DIR) && \
	make install

$(CURL_DIR)/Makefile: $(CURL_DIR)/configure
	cd $(FULL_ROOT_DIR) && \
	cd $(CURL_DIR) && \
	./configure --prefix=$(CURL_INSTALL_DIR) --with-gnutls

$(CURL_DIR)/configure:
	cd $(FULL_ROOT_DIR) && \
	cd $(CURL_DIR) && \
	autoreconf -fi

cleancurl:
	rm -f $(CURL_DIR)/Makefile
	rm -r -f $(CURL_INSTALL_DIR)