ROOT_DIR= ../..
OBJ_DIR= $(ROOT_DIR)/obj/unix
OUT_DIR= $(ROOT_DIR)/out/unix
LIB_DIR= $(ROOT_DIR)/lib/unix
INSTALL_DIR= /usr/bin
EXTERN_DIR= $(ROOT_DIR)/extern
CURL_DIR= $(EXTERN_DIR)/curl
EXTERN_OUT_DIR= $(ROOT_DIR)/externout
CURL_INSTALL_DIR= $(PWD)/$(EXTERN_OUT_DIR)/curl
CURL_INC_DIR= $(CURL_INSTALL_DIR)/include
CURL_LIB_DIR= $(CURL_INSTALL_DIR)/lib
FULL_ROOT_DIR= $(PWD)
ARGSPARSER_DIR= $(ROOT_DIR)/ArgsParser
GTK_DIR= $(EXTERN_DIR)/gtk
GTK_INSTALL_DIR= $(PWD)/$(EXTERN_OUT_DIR)/gtk
GTK_MESON_BUILD_DIR= $(FULL_ROOT_DIR)/$(GTK_DIR)/builddir

ARGSPARSER_SRC_DIR= $(ARGSPARSER_DIR)/src
ARGSPARSER_INC_DIR= $(ARGSPARSER_DIR)/include
ARGSPARSER_OBJ_DIR= $(OBJ_DIR)/ArgsParser

SRC_DIR= $(ROOT_DIR)/src
INC_DIR= $(ROOT_DIR)/include

MAKE= make
SOEXT= so
CXX= g++
CXXEXT= cpp
CXXFLAGS= -c -I$(INC_DIR) -I$(CURL_INC_DIR) -I$(ARGSPARSER_INC_DIR)

LD= g++
LDEXT= o
LDFLAGS= -L$(CURL_LIB_DIR) -L$(LIB_DIR) -Wl,-rpath=.
LDLIBS= -lcurl -lArgsParser

SRC_FILES= $(shell find $(SRC_DIR) -maxdepth 1 -type f -name *.$(CXXEXT))
OBJ_FILES= $(patsubst $(SRC_DIR)/%.$(CXXEXT), $(OBJ_DIR)/%.$(LDEXT), $(SRC_FILES))

ARGSPARSER_SRC_FILES= $(shell find $(ARGSPARSER_SRC_DIR) -maxdepth 1 -type f -name *.$(CXXEXT))
ARGSPARSER_OBJ_FILES= $(patsubst $(ARGSPARSER_SRC_DIR)/%.$(CXXEXT), $(ARGSPARSER_OBJ_DIR)/%.$(LDEXT), $(ARGSPARSER_SRC_FILES))
ARGSPARSER_OUT_NAME= libArgsParser.$(SOEXT)
ARGSPARSER_OUT_FILE= $(LIB_DIR)/$(ARGSPARSER_OUT_NAME)

OUT_NAME= download
OUT_FILE= $(OUT_DIR)/$(OUT_NAME)

.PHONY: all clean cleanobj cleanlib rebuild install uninstall run buildcurl cleancurl buildgtk cleangtk

all: $(OUT_FILE)

$(OUT_FILE): $(OBJ_FILES) $(ARGSPARSER_OUT_FILE)
	mkdir -p $(OUT_DIR)
	$(LD) $(LDFLAGS) $(OBJ_FILES) -o $(OUT_FILE) $(LDLIBS)
	mkdir -p $(OUT_DIR)
	cp $(ARGSPARSER_OUT_FILE) $(OUT_DIR)/$(ARGSPARSER_OUT_NAME)

$(OBJ_FILES): buildcurl buildgtk
$(OBJ_DIR)/%.$(LDEXT): $(SRC_DIR)/%.$(CXXEXT)
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(ARGSPARSER_OUT_FILE): $(ARGSPARSER_OBJ_FILES)
	mkdir -p $(LIB_DIR)
	$(CXX) -shared $(ARGSPARSER_OBJ_FILES) -o $(ARGSPARSER_OUT_FILE)

$(ARGSPARSER_OBJ_FILES):
$(ARGSPARSER_OBJ_DIR)/%.$(LDEXT): $(ARGSPARSER_SRC_DIR)/%.$(CXXEXT)
	mkdir -p $(ARGSPARSER_OBJ_DIR)
	$(CXX) $(CXXFLAGS) -fPIC $< -o $@

clean: cleanobj cleancurl cleanlib cleangtk
	rm -r -f $(OUT_DIR)

cleanobj:
	rm -r -f $(OBJ_DIR)

cleanlib:
	rm -r -f $(LIB_DIR)

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

buildgtk: $(GTK_DIR)/mesonBuild.txt
	cd $(FULL_ROOT_DIR) && \
	cd $(GTK_DIR)/builddir && \
	ninja install

$(GTK_DIR)/mesonBuild.txt:
	cd $(FULL_ROOT_DIR) && \
	cd $(GTK_DIR) && \
	meson setup --prefix $(GTK_INSTALL_DIR) $(GTK_MESON_BUILD_DIR)
	touch $(GTK_DIR)/mesonBuild.txt

cleangtk:
	rm -r -f $(GTK_MESON_BUILD_DIR)
	rm -r -f $(GTK_DIR)/mesonBuild.txt