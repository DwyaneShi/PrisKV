LIBJSONC_OUTPUT = $(LIBJSONC_DIR)/build
ifeq ($(__OS_TYPE), redhat) # __OS_TYPE from ./Makefile
LIBJSONC = $(LIBJSONC_OUTPUT)/lib64/libjson-c.a
else ifeq ($(__OS_TYPE), velinux)
LIBJSONC = $(LIBJSONC_OUTPUT)/lib64/libjson-c.a
else
LIBJSONC = $(LIBJSONC_OUTPUT)/lib/libjson-c.a
endif
LIBJSONC_HEADERS = $(LIBJSONC_OUTPUT)/include/json-c/json.h
LIBJSONC_HEADERS_ = json-c/json.h
$(LIBJSONC_HEADERS): $(LIBJSONC)
$(LIBJSONC_HEADERS_): $(LIBJSONC)
$(LIBJSONC):
	git submodule update --init $(LIBJSONC_DIR)
	mkdir -p $(LIBJSONC_OUTPUT) && cd $(LIBJSONC_OUTPUT) \
	&& ../cmake-configure --prefix=. --disable-werror \
	&& make -j && make install

DEPS_STATIC_LIBS += $(LIBJSONC)
INCFLAGS += -I$(LIBJSONC_OUTPUT)/include -I$(LIBJSONC_OUTPUT)/include/json-c
