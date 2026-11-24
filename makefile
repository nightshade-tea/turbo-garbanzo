# variables ------------------------------------------------------------------ #

# debug build if `debug` is one of the goals
DEBUG := $(filter debug,$(MAKECMDGOALS))

# project root, source, include and build directories
root  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
src   := $(root)/src
inc   := $(root)/include

ifdef DEBUG
build := $(root)/debug
else
build := $(root)/build
endif

# sources, headers, objects, header dependencies
srcs := $(shell find $(src) -name '*.c')
hdrs := $(shell find $(src) $(inc) -name '*.h')
objs := $(srcs:$(src)/%.c=$(build)/obj/%.o)
deps := $(objs:.o=.d)

# static libraries: src/<lib>/ -> build/<lib>.a, linked into every program
libs  := net
arqs  := $(libs:%=$(build)/%.a)

# programs: src/<prog>/ -> build/<prog>
progs := ping pong
bins  := $(progs:%=$(build)/%)

# settings ------------------------------------------------------------------- #

# compiler, preprocessor flags, compiler flags
CC       := gcc
CPPFLAGS += -I$(inc) -MMD -MP -D_DEFAULT_SOURCE
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

# compile: src/<path>.c -> build/obj/<path>.[od]
$(build)/obj/%.o : $(src)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# archive: build/obj/<lib>/*.o -> build/<lib>.a
$(foreach l,$(libs),$(eval $(build)/$(l).a : $(call objs_of,$(l))))
$(arqs) :
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

# link: build/obj/<prog>/*.o + build/<lib>.a -> build/<prog>
$(foreach p,$(progs),$(eval $(build)/$(p) : $(call objs_of,$(p)) $(arqs)))
$(bins) :
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY : all debug format clean

all debug : $(bins)

format :
	clang-format -i $(srcs) $(hdrs)

clean :
	rm -rf $(root)/build $(root)/debug

# header dependencies from previous builds
-include $(deps)
