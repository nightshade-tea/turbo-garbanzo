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

# static library built from $(src)/net, linked into every program
lib := $(build)/net.a

# progs = directories under $(src) that contain at least one .c file,
# except the library directory
prog_dirs := $(sort $(dir $(wildcard $(src)/*/*.c)))
progs     := $(filter-out net,$(notdir $(prog_dirs:/=)))
bins      := $(addprefix $(build)/,$(progs))

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
prog_objs = $(filter $(build)/obj/$(1)/%,$(objs))

# targets -------------------------------------------------------------------- #

# compile: src/<path>.c -> build/obj/<path>.[od]
$(build)/obj/%.o : $(src)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# archive: build/obj/net/*.o -> build/net.a
$(lib) : $(call prog_objs,net)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

# link: build/obj/<prog>/*.o + build/net.a -> build/<prog>
# (the library must come after the objects on the link line)
$(foreach b,$(bins),$(eval $(b) : $(call prog_objs,$(notdir $(b)))))
$(bins) : $(lib)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(lib) $(LDLIBS)

.PHONY : all debug format clean

all debug : $(bins)

format :
	clang-format -i $(srcs) $(hdrs)

clean :
	rm -rf $(root)/build $(root)/debug

# header dependencies from previous builds
-include $(deps)
