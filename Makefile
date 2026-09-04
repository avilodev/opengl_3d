CC       := x86_64-w64-mingw32-gcc
CFLAGS   := -Wall -Wextra -std=c11 -O2 -MMD -MP
CPPFLAGS := -Iinclude
LDFLAGS  := -Linclude/lib
LDLIBS   := -lglfw3 -lopengl32 -lgdi32 -lm

TARGET := bin/Game.exe

# Stated rather than left to "the first target in the file", because it is no
# longer the first target in the file: the probe rules sit above `all`, and one
# of them would otherwise become what a bare `make` does.
.DEFAULT_GOAL := all
BUILD_DIR := build
OBJ_DIR := obj

# Directories that are part of the build. Adding a .c file to one of these
# needs no change here; adding a new module directory needs one word.
SRC_DIRS := src src/render src/gameplay src/input src/states

# Modules taken one file at a time, because their directory holds more than the
# build wants: src/core also has dialogue, save state and inventory, none of
# which is linked yet.
SRC_FILES := src/core/json.c

SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c)) $(SRC_FILES)
OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# ---------------------------------------------------------------------------
# The headless probes
#
# Six programs that check arithmetic without a window: which way a body bends,
# whether a pose jumps when a clip changes, what a swing hits, where a scabbard
# is worn, whether a weapon's spec agrees with its mesh. All built with the HOST
# compiler rather than the cross one, because the point is to run them here.
#
# They link one list between them rather than six.
#
# There used to be a hand-maintained source list per probe - about a hundred
# lines of them - and the failure mode was always the same: a module split in
# two, or a new file added, and the six lists went out of step one at a time.
# Whichever probe was not edited failed to link, and it failed to link later,
# when somebody happened to run it. Six lists that must agree are five chances
# to disagree.
#
# One list costs a few seconds of compiling per probe, and nothing else: these
# are small translation units and none of them touches GL. What it buys is that
# splitting weapon.c into three, or adding a module, is a line here and nothing
# anywhere else.
#
# The rule for this list is exactly "does it compile without a GL context". A
# file that needs one belongs in the game build and not in a probe; that is why
# the placement search lives in arm_placement.c apart from the renderer that
# calls it, and why the _glb readers are split from the modules that upload
# what they read.
HEADLESS_CORE := \
    src/core/json.c \
    src/gameplay/action_track.c \
    src/gameplay/arena.c \
    src/gameplay/combatant.c \
    src/gameplay/damage_number.c \
    src/gameplay/melee_swing.c \
    src/gameplay/player_combatant.c \
    src/gameplay/player_controller.c \
    src/gameplay/player_grapple.c \
    src/gameplay/player_locomotion.c \
    src/gameplay/player_mantle.c \
    src/gameplay/player_slide.c \
    src/gameplay/player_wallclimb.c \
    src/gameplay/player_wallrun.c \
    src/gameplay/sights.c \
    src/gameplay/spawn_select.c \
    src/gameplay/third_person_camera.c \
    src/gameplay/traversal.c \
    src/gameplay/viewmodel_pose.c \
    src/gameplay/weapon.c \
    src/gameplay/weapon_firearm.c \
    src/gameplay/weapon_melee.c \
    src/gameplay/weapon_placement.c \
    src/gameplay/weapon_registry.c \
    src/gameplay/weapon_spec.c \
    src/gameplay/weapon_specs.c \
    src/render/arm_placement.c \
    src/render/character_animation.c \
    src/render/character_transform.c \
    src/render/glb.c \
    src/render/skinned_model_glb.c \
    src/render/skinned_pose.c \
    src/render/static_model_glb.c \
    src/render/stb_image.c \
    src/render/viewmodel_clip_arms.c \
    src/render/viewmodel_neck.c \
    src/render/weapon_grip.c

PROBE_CC := gcc
# -MMD -MP so a probe rebuilds when a HEADER it reads changes, not only when a
# .c does. Without it, editing weapon_spec.h and running a probe re-ran the
# stale binary and reported a pass on code that no longer existed - which is
# the most expensive kind of green there is.
PROBE_CFLAGS := -Wall -Wextra -std=c11 -O1 -MMD -MP

# Every probe: the target name, the tool that holds its main, and one line each.
# All of them run from the project root, for the relative model paths the game
# uses.
#
#   probe        the viewmodel's arithmetic - see tools/viewmodel_probe.c
#   bodyprobe    the character rig's, checked the same way
#   sheathprobe  where the scabbard is worn and whether the sword agrees
#   meleeprobe   what a swing hits: once per target, only inside the window
#   deathprobe   health, death and respawn
#   specprobe    every weapon says what it is, and its mesh agrees
PROBE       := bin/viewmodel_probe
BODY_PROBE  := bin/body_probe
SHEATH_PROBE:= bin/sheath_probe
MELEE_PROBE := bin/melee_probe
DEATH_PROBE := bin/death_probe
SPEC_PROBE  := bin/weapon_spec_probe
# The one probe built with the tuning overlay compiled in - see
# tools/placement_probe.c. A check on the overlay that compiled it out would be
# a check that passes by not running.
TUNE_PROBE  := bin/placement_probe

PROBES := $(PROBE) $(BODY_PROBE) $(SHEATH_PROBE) $(MELEE_PROBE) \
          $(DEATH_PROBE) $(SPEC_PROBE) $(TUNE_PROBE)

# One recipe for all six. $* is the probe's own source stem.
bin/%: tools/%.c $(HEADLESS_CORE)
	@mkdir -p bin
	$(PROBE_CC) $(PROBE_CFLAGS) $(CPPFLAGS) $< $(HEADLESS_CORE) -o $@ -lm

probe:       $(PROBE);       ./$(PROBE)
bodyprobe:   $(BODY_PROBE);  ./$(BODY_PROBE)
sheathprobe: $(SHEATH_PROBE);./$(SHEATH_PROBE)
meleeprobe:  $(MELEE_PROBE); ./$(MELEE_PROBE)
deathprobe:  $(DEATH_PROBE); ./$(DEATH_PROBE)
specprobe:   $(SPEC_PROBE);  ./$(SPEC_PROBE)

# Records what the weapons currently derive to. Run it when a change to a
# shared number was MEANT - a retuned sight moves every gun carrying it, and
# the point of the snapshot is that it says so.
golden: $(SPEC_PROBE)
	./$(SPEC_PROBE) --update
	@git --no-pager diff --stat tools/golden/ || true

$(TUNE_PROBE): tools/placement_probe.c $(HEADLESS_CORE)
	@mkdir -p bin
	$(PROBE_CC) $(PROBE_CFLAGS) $(CPPFLAGS) -DWEAPON_TUNING $< \
	    $(HEADLESS_CORE) -o $@ -lm
tuneprobe:   $(TUNE_PROBE);  ./$(TUNE_PROBE)

# Everything the probes check, in one word. What to run before a commit.
probes: probe bodyprobe sheathprobe meleeprobe deathprobe specprobe \
        tuneprobe

.PHONY: all tune build clean run probes golden \
        probe bodyprobe sheathprobe meleeprobe deathprobe specprobe tuneprobe

all: $(TARGET)

# The same game with the placement overlay compiled in: it reads any
# placement.json sitting beside an asset, F5 re-reads one, and F6 prints it
# back as C to paste into weapon_specs.c. See gameplay/weapon_placement.h.
#
# A separate target and a separate object directory, so that switching between
# tuning and shipping is not a full rebuild each way - and so that `make` can
# never accidentally produce the build that opens files.
tune:
	$(MAKE) OBJ_DIR=obj-tune TARGET=bin/GameTuning.exe \
	    CPPFLAGS='$(CPPFLAGS) -DWEAPON_TUNING' all

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: all
	$(TARGET)

# Assemble a self-contained folder whose working directory matches the runtime
# paths used by the game. Recreated each time so removed or renamed assets
# cannot linger in a package from an earlier build.
#
# models/ is filtered rather than copied whole. The directory is 165MB and the
# game opens six files out of it: the rest is source FBX, .blend files, the
# artist's texture sets, the _RM animation variants that are deliberately not
# loaded, and now the import manifests and measurements. None of that is a
# runtime asset - the importer folds the textures INTO the .glb - and shipping
# it put a hundred and sixty megabytes of somebody else's licensed source art
# into every build.
#
# Every .glb rather than the six named ones, deliberately: which meshes a
# weapon names is a fact about weapon_specs.c, and a package that had to be
# told would be a package that silently missed the next one. Copying twenty
# megabytes and never being wrong beats copying five and sometimes being wrong.
RUNTIME_DIRS := assets data

build: all
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	cp $(TARGET) $(BUILD_DIR)/Game.exe
	cp -R $(RUNTIME_DIRS) $(BUILD_DIR)/
	cd models && find . -name '*.glb' -print0 | \
	    tar --null -cf - --files-from=- | (cd ../$(BUILD_DIR) && mkdir -p models && tar -xf - -C models)
	@echo "packaged $$(find $(BUILD_DIR) -type f | wc -l) files, $$(du -sh $(BUILD_DIR) | cut -f1)"

clean:
	rm -rf obj obj-tune $(TARGET) bin/GameTuning.exe \
	    $(PROBES) $(PROBES:%=%.d) $(BUILD_DIR)

-include $(DEPS)
-include $(PROBES:%=%.d)
