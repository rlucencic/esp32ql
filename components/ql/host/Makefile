# Host build of the QL core: make -C components/ql/host run
# (ROM= an absolute path or one relative to this folder)
ROOT      := ../../..
MUSASHI   := ../../../../esp32emu/components/musashi
EMUDBG    := ../../../../esp32emu/components/emudbg
QL        := ..
BUILD     := build
CFLAGS    := -O2 -g -Wall -DM68K_INSTRUCTION_HOOK=2 -I$(QL)/include -I$(QL) -I$(MUSASHI) -I$(MUSASHI)/musashi \
             -I$(MUSASHI)/generated -I$(EMUDBG)/include
ROM       ?= $(ROOT)/roms/js.rom
FRAMES    ?= 500

QL_SRCS   := $(wildcard $(QL)/*.c)
M68K_SRCS := $(MUSASHI)/musashi/m68kcpu.c $(MUSASHI)/musashi/m68kdasm.c \
             $(MUSASHI)/musashi/softfloat/softfloat.c $(MUSASHI)/generated/m68kops.c \
             $(MUSASHI)/m68k_dbg.c
QL_OBJS   := $(patsubst $(QL)/%.c,$(BUILD)/ql/%.o,$(QL_SRCS))
M68K_OBJS := $(patsubst $(MUSASHI)/%.c,$(BUILD)/m68k/%.o,$(M68K_SRCS)) \
             $(BUILD)/emudbg.o $(BUILD)/emudbg_posix.o

run: $(BUILD)/ql_host
	./$(BUILD)/ql_host $(ROM) $(FRAMES) $(BUILD)/screen.ppm

$(BUILD)/ql_host: host_main.c $(QL_OBJS) $(M68K_OBJS)
	$(CC) $(CFLAGS) $^ -lm -lpthread -o $@

$(BUILD)/ql/%.o: $(QL)/%.c $(wildcard $(QL)/*.h $(QL)/include/*.h) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/m68k/%.o: $(MUSASHI)/%.c $(MUSASHI)/m68kconf.h $(EMUDBG)/include/emudbg.h $(wildcard $(MUSASHI)/musashi/*.h) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/%.o: $(EMUDBG)/%.c $(EMUDBG)/include/emudbg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: run clean
