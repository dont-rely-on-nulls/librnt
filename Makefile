CC = gcc
AR = ar

CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic

PREFIX ?= /usr/local
DESTDIR ?=
BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/librnt.a
SOURCES := $(wildcard src/*.c)
HEADERS := $(wildcard include/*.h)
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
TESTS := $(patsubst tests/%.c,$(BUILD_DIR)/tests/%,$(wildcard tests/*.c))

.PHONY: all clean install test
all: $(LIBRARY)

$(LIBRARY): $(OBJECTS) Makefile | $(BUILD_DIR)
	$(AR) rcs $@ $(OBJECTS)

$(BUILD_DIR)/%.o: src/%.c Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR) $(BUILD_DIR)/tests:
	mkdir -p $@

# Runs every test program, and fails if any of them does.
test: $(TESTS)
	@failed=0; for t in $(TESTS); do echo "$$t"; $$t || failed=1; done; exit $$failed

$(BUILD_DIR)/tests/%: tests/%.c tests/check.h $(HEADERS) $(LIBRARY) | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $< $(LIBRARY) -o $@

install: $(LIBRARY)
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	install -m 644 $(LIBRARY) $(DESTDIR)$(PREFIX)/lib/librnt.a
	for header in $(HEADERS); do \
		install -m 644 "$$header" $(DESTDIR)$(PREFIX)/include/; \
	done
	install -d $(DESTDIR)$(PREFIX)/share/licenses/librnt
	install -m 644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/librnt/LICENSE

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)
