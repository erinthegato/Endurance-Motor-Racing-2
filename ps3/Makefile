# Endurance Motor Racing 2.0 - PS3 build (PSL1GHT).
#   make -C ps3 pkg   ->  ps3/build/EnduranceMotorRacing2.pkg  (install with Package Manager / RPCS3)
# Needs $PSL1GHT / $PS3DEV (ps3toolchain + PSL1GHT), zlib in $PS3DEV/portlibs/ppu, and NVIDIA's Cg
# library (libCg.so) for cgcomp. The same game code as the Vita build; only src/platform_ps3.cpp and
# ps3/shaders are PS3-specific. GitHub Actions builds it on every push (.github/workflows/build.yml).
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

include $(PSL1GHT)/ppu_rules

TARGET		:=	EnduranceMotorRacing2
TITLE		:=	Endurance Motor Racing 2.0
APPID		:=	EMRW00027
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
APP_VER		?=	01.00

ROOT		:=	$(abspath $(CURDIR)/..)
BUILD		:=	$(CURDIR)/build
BUILDDIR	:=	$(BUILD)
ICON0		:=	$(CURDIR)/pkg/ICON0.PNG
SFOXML		:=	$(BUILD)/sfo.xml
PKGFILES	:=	$(BUILD)/pkgfiles

GAME		:=	data track race racecontrol launch reliability champ career settings menu sound render \
				render_fx clip game intro trophies setups platform_ps3
SHADERS		:=	emr_vp.vpo emr_fp_tex.fpo emr_fp_col.fpo
OFILES		:=	$(addprefix $(BUILD)/,$(addsuffix .o,$(SHADERS))) $(addprefix $(BUILD)/,$(addsuffix .o,$(GAME)))

CXXFLAGS	:=	-O2 -std=gnu++17 -Wall -Wno-misleading-indentation -mcpu=cell $(MACHDEP) $(CXXFLAGS_INIT) \
				-ffast-math -fno-strict-aliasing -fno-exceptions -fno-rtti -DPLATFORM_PS3=1 \
				-I$(ROOT)/src -I$(BUILD) $(LIBPSL1GHT_INC) -I$(PORTLIBS)/include
# every fopen / remove goes through the path mapper in platform_ps3.cpp
LDFLAGS		:=	$(MACHDEP) -Wl,--wrap=fopen,--wrap=remove -Wl,-Map,$(BUILD)/$(TARGET).map
LIBPATHS	:=	$(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib
LIBS		:=	-lrsx -lgcm_sys -lio -lsysutil -laudio -lz -lrt -llv2 -lm

.PHONY: all pkg clean
all: $(BUILD)/$(TARGET).self
pkg: $(BUILD)/$(TARGET).pkg

$(BUILD):
	@mkdir -p $@

# shaders: Cg -> RSX programs (cgcomp), linked in as data with a header for each
$(BUILD)/%.vpo: shaders/%.vcg | $(BUILD)
	@echo $(notdir $<)
	@$(CGCOMP) -v $< $@
$(BUILD)/%.fpo: shaders/%.fcg | $(BUILD)
	@echo $(notdir $<)
	@$(CGCOMP) -f $< $@
$(BUILD)/%.vpo.o $(BUILD)/%.fpo.o: | $(BUILD)
$(BUILD)/%.vpo.o: $(BUILD)/%.vpo
	@cd $(BUILD) && bin2s -a 64 $*.vpo | $(AS) -o $*.vpo.o
	@echo "extern const u8 $*_vpo[]; extern const u8 $*_vpo_end[]; extern const u32 $*_vpo_size;" > $(BUILD)/$*_vpo.h
$(BUILD)/%.fpo.o: $(BUILD)/%.fpo
	@cd $(BUILD) && bin2s -a 64 $*.fpo | $(AS) -o $*.fpo.o
	@echo "extern const u8 $*_fpo[]; extern const u8 $*_fpo_end[]; extern const u32 $*_fpo_size;" > $(BUILD)/$*_fpo.h

# the game
$(BUILD)/platform_ps3.o: $(addprefix $(BUILD)/,$(addsuffix .o,$(SHADERS)))
$(BUILD)/%.o: $(ROOT)/src/%.cpp $(wildcard $(ROOT)/src/*.h) | $(BUILD)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	@echo linking ... $(notdir $@)
	@$(CXX) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@

# the package: EBOOT.BIN, PARAM.SFO, ICON0 / PIC1 and the game's data in USRDIR/ (DATA: the shared
# set and the music, HD: the Vita's higher-detail set, PS3: the PS3's own denser car models, from ps3/data)
$(BUILD)/$(TARGET).pkg: $(BUILD)/pkgfiles.stamp $(SFOXML)
$(BUILD)/pkgfiles.stamp: $(BUILD)/$(TARGET).self
	@echo staging data ...
	@rm -rf $(PKGFILES) && mkdir -p $(PKGFILES)/USRDIR
	@cp -r $(ROOT)/DATA $(ROOT)/HD $(PKGFILES)/USRDIR/ && cp -r $(CURDIR)/data $(PKGFILES)/USRDIR/PS3
	@cp $(CURDIR)/pkg/PIC1.PNG $(PKGFILES)/
	@touch $@
$(SFOXML): | $(BUILD)
	@python3 -c "import re,sys; s=open(sys.argv[1]).read(); s=re.sub(r'(name=.(APP_VER|VERSION). type=.string.>)\s*[^<]*?\s*(</value>)', lambda m: m.group(1) + sys.argv[2] + m.group(3), s); open(sys.argv[3], 'w').write(s)" $(PS3DEV)/bin/sfo.xml $(APP_VER) $@

clean:
	rm -rf $(BUILD)
