CC = gcc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -fPIC -fvisibility=hidden
PREFIX ?= /usr/local

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
TESTS := $(patsubst tests/%.c,build/tests/%,$(wildcard tests/*.c))

ifeq ($(shell uname -s),Darwin)
LIBRARY := librnt.dylib
SHARED := -dynamiclib -install_name $(PREFIX)/lib/$(LIBRARY)
else
LIBRARY := librnt.so
SHARED := -shared -Wl,-soname,$(LIBRARY)
endif

.PHONY: all test install clean
all: build/$(LIBRARY)

build/$(LIBRARY): $(OBJECTS)
	$(CC) $(SHARED) $(LDFLAGS) $^ -o $@

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Tests link the objects, so they obviously reach what the library
# hides
build/tests/%: tests/%.c $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -MMD -MP $< $(OBJECTS) -o $@

test: $(TESTS)
	@for t in $^; do echo $$t; $$t || exit 1; done

install: all
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/share/licenses/librnt
	install -m 755 build/$(LIBRARY) $(DESTDIR)$(PREFIX)/lib/
	install -m 644 include/rnt.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/librnt/

clean:
	rm -rf build

-include $(OBJECTS:.o=.d) $(TESTS:=.d)
