# variables ------------------------------------------------------------------ #

# debug build if `debug` is one of the goals
DEBUG := $(filter debug,$(MAKECMDGOALS))

# project root, library, source, include and build directories
root  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
lib   := $(root)/lib
src   := $(root)/src
inc   := $(root)/include

ifdef DEBUG
build := $(root)/debug
else
build := $(root)/build
endif

# sources, headers, objects, header dependencies
srcs := $(shell find $(lib) $(src) -name '*.c')
hdrs := $(shell find $(lib) $(src) $(inc) -name '*.h')
objs := $(srcs:$(root)/%.c=$(build)/obj/%.o)
deps := $(objs:.o=.d)

# static libraries: lib/<lib>/ -> build/lib/<lib>.a
libs  := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard $(lib)/*/*.c)))))
arqs  := $(libs:%=$(build)/lib/%.a)

# programs: src/<prog>.c -> build/bin/<prog>
progs := $(sort $(basename $(notdir $(wildcard $(src)/*.c))))
bins  := $(progs:%=$(build)/bin/%)

# libraries per program, dependents first (static link order)
loadb_libs   := tbl net
monitor_libs := tbl net
ping_libs    := net
pong_libs    := mon net
table_libs   := net

# settings ------------------------------------------------------------------- #

# compiler, preprocessor flags, compiler flags
CC       := gcc
CPPFLAGS += -I$(inc) -MMD -MP -D_GNU_SOURCE
CFLAGS   += -std=c23 -pthread -Wall -Wextra -Wconversion -Wstrict-prototypes \
            -Wmissing-prototypes -Wshadow -Wundef -Wdouble-promotion
LDFLAGS  += -pthread

ifdef DEBUG
CPPFLAGS += -DDEBUG
CFLAGS   += -O0 -g -fsanitize=address,undefined
LDFLAGS  += -fsanitize=address,undefined
else
CFLAGS   += -O2
endif

# default target
.DEFAULT_GOAL := all

# functions ------------------------------------------------------------------ #

# $(1) -> objects belonging to $(1)
objs_of = $(filter $(build)/obj/$(1)/%,$(objs))

# targets -------------------------------------------------------------------- #

# compile: <path>.c -> build/obj/<path>.[od]
$(build)/obj/%.o : $(root)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# archive: build/obj/lib/<lib>/*.o -> build/lib/<lib>.a
$(foreach l,$(libs),$(eval $(build)/lib/$(l).a : $(call objs_of,lib/$(l))))
$(arqs) :
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

# link: build/obj/src/<prog>.o + build/lib/<lib>.a -> build/bin/<prog>
$(foreach p,$(progs),$(eval $(build)/bin/$(p) : $(build)/obj/src/$(p).o $($(p)_libs:%=$(build)/lib/%.a)))
$(bins) :
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY : all debug format clean

all debug : $(arqs) $(bins)

format :
	clang-format -i $(srcs) $(hdrs)

clean :
	rm -rf $(root)/build $(root)/debug

# header dependencies from previous builds
-include $(deps)
