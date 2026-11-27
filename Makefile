# Rebuild orig/EBOOT.elf byte-for-byte from the splat output and check its SHA1.
#   make setup   - create .venv and install splat
#   make split   - run splat (regenerates asm/, assets/, eboot.ld) + reloc override passes
#   make check   - assemble, link, objcopy and compare against orig/EBOOT.elf

CROSS   ?= mipsel-linux-gnu-
AS      := $(CROSS)as
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
PYTHON  := .venv/bin/python

BUILD   := build
TARGET  := $(BUILD)/eboot.elf
ROM     := $(BUILD)/EBOOT.rebuilt.elf
BASEROM := orig/EBOOT.elf
# must match string_encoding in config/eboot.yaml
STRING_ENCODING := cp932

ASFLAGS := -EL -march=allegrex -mabi=32 -G0 -no-pad-sections -Iinclude

S_FILES   := $(shell find asm -name '*.s' 2>/dev/null)
BIN_FILES := $(shell find assets -name '*.bin' 2>/dev/null)
O_FILES   := $(S_FILES:%.s=$(BUILD)/%.o) $(BIN_FILES:%.bin=$(BUILD)/%.o)

.PHONY: all check clean setup split
all: check

setup:
	python3 -m venv .venv
	.venv/bin/pip install -r requirements.txt

# Split, then add reloc overrides/ignores for false symbol references (PRX relocation table)
# and re-split until no new ones appear. See tools/gen_reloc_overrides.py.
GEN_RELOCS := python3 tools/gen_reloc_overrides.py $(BASEROM) asm config/reloc_addrs_auto.txt config/reloc_addrs.txt
split:
	rm -rf asm assets eboot.ld
	$(PYTHON) -m splat split config/eboot.yaml
	@for i in 1 2 3; do \
		out=$$($(GEN_RELOCS)) || exit 1; echo "$$out"; \
		case "$$out" in *"(0 new)"*) exit 0;; esac; \
		rm -rf asm assets eboot.ld; $(PYTHON) -m splat split config/eboot.yaml || exit 1; \
	done; echo "reloc overrides did not converge"; exit 1

# GNU as can't take some splat output as-is (VFPU, LE NaN doubles, SJIS strings): see tools/asm_fixups.py
$(BUILD)/%.o: %.s tools/asm_fixups.py
	@mkdir -p $(dir $@)
	python3 tools/asm_fixups.py --encoding $(STRING_ENCODING) $< $(BUILD)/$*.fixed.s
	$(AS) $(ASFLAGS) -o $@ $(BUILD)/$*.fixed.s

$(BUILD)/%.o: %.bin
	@mkdir -p $(dir $@)
	$(LD) -EL -r -b binary -o $@ $<

$(TARGET): $(O_FILES) eboot.ld
	$(LD) -EL -T eboot.ld -T config/undefined_syms_auto.txt -T config/undefined_funcs_auto.txt \
		-Map $(BUILD)/eboot.map --no-check-sections -o $@

$(ROM): $(TARGET)
	$(OBJCOPY) -O binary $< $@

check: $(ROM)
	@sha1sum $(ROM) $(BASEROM)
	@if cmp -s $(ROM) $(BASEROM); then echo "OK: rebuild matches $(BASEROM)"; \
	else echo "MISMATCH"; cmp -l $(ROM) $(BASEROM) | head; exit 1; fi

clean:
	rm -rf $(BUILD)
