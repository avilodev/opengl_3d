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
RUNTIME_DIRS := assets data models

# Directories that are part of the build. Adding a .c file to one of these
# needs no change here; adding a new module directory needs one word.
SRC_DIRS := src src/render src/gameplay src/input src/states

# Modules taken one file at a time, because their directory holds more than the
# build wants: src/core also has dialogue, save state and inventory, none of
# which is linked yet.
SRC_FILES := src/core/json.c

SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c)) $(SRC_FILES)
OBJS := $(patsubst src/%.c,obj/%.o,$(SRCS))
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
PROBE_CFLAGS := -Wall -Wextra -std=c11 -O1

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

PROBES := $(PROBE) $(BODY_PROBE) $(SHEATH_PROBE) $(MELEE_PROBE) \
          $(DEATH_PROBE) $(SPEC_PROBE)

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

# Everything the probes check, in one word. What to run before a commit.
probes: probe bodyprobe sheathprobe meleeprobe deathprobe specprobe

.PHONY: all build clean run probes \
        probe bodyprobe sheathprobe meleeprobe deathprobe specprobe

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: all
	$(TARGET)

# Assemble a self-contained folder whose working directory matches the runtime
# paths used by the game. Recreate it so removed or renamed assets cannot linger
# in a package from an earlier build.
build: all
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	cp $(TARGET) $(BUILD_DIR)/Game.exe
	cp -R $(RUNTIME_DIRS) $(BUILD_DIR)/

clean:
	rm -rf obj $(TARGET) $(PROBES) $(BUILD_DIR)

-include $(DEPS)
