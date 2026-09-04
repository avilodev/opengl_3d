CC       := x86_64-w64-mingw32-gcc
CFLAGS   := -Wall -Wextra -std=c11 -O2 -MMD -MP
CPPFLAGS := -Iinclude
LDFLAGS  := -Linclude/lib
LDLIBS   := -lglfw3 -lopengl32 -lgdi32 -lm

TARGET := bin/Game.exe
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

# The headless probe: the viewmodel's arithmetic checked without a window. Built
# with the HOST compiler, not the cross one, because the point is to run it here
# - it links only the parts that need no GL context, which is why the placement
# search lives in its own file. See tools/viewmodel_probe.c.
PROBE := bin/viewmodel_probe
PROBE_CC := gcc
PROBE_SRCS := tools/viewmodel_probe.c src/render/arm_placement.c \
              src/render/viewmodel_clip_arms.c src/render/viewmodel_neck.c \
              src/render/character_transform.c src/render/character_animation.c \
              src/render/weapon_grip.c \
              src/gameplay/viewmodel_pose.c src/gameplay/weapon_spec.c \
              src/gameplay/weapon_specs.c src/gameplay/sights.c \
              src/render/skinned_model_glb.c src/render/skinned_pose.c \
              src/render/glb.c src/render/stb_image.c src/core/json.c \
              src/gameplay/weapon.c src/gameplay/melee_swing.c \
              src/gameplay/traversal.c src/gameplay/arena.c \
              src/gameplay/action_track.c src/gameplay/combatant.c \
              src/gameplay/damage_number.c src/gameplay/third_person_camera.c

# The character rig's arithmetic, checked the same way and for the same reason
# the viewmodel's is: which way a body bends is a sign, and whether a pose jumps
# when a clip changes is a subtraction. Neither is something to settle by eye.
BODY_PROBE := bin/body_probe
BODY_PROBE_SRCS := tools/body_probe.c src/render/character_animation.c \
              src/render/character_transform.c src/render/weapon_grip.c \
              src/render/arm_placement.c \
              src/gameplay/weapon_specs.c src/gameplay/weapon_spec.c \
              src/gameplay/viewmodel_pose.c \
              src/gameplay/sights.c \
              src/render/skinned_model_glb.c src/render/skinned_pose.c \
              src/render/glb.c src/render/stb_image.c src/core/json.c

# Where the scabbard is worn and whether the sword in it agrees. Three facts
# that have to line up and cannot be seen lining up by playing - see
# tools/sheath_probe.c.
SHEATH_PROBE := bin/sheath_probe
SHEATH_PROBE_SRCS := tools/sheath_probe.c src/render/character_transform.c \
              src/render/weapon_grip.c \
              src/render/arm_placement.c src/gameplay/action_track.c \
              src/render/character_animation.c \
              src/gameplay/weapon_specs.c src/gameplay/weapon_spec.c \
              src/gameplay/viewmodel_pose.c src/gameplay/sights.c \
              src/render/skinned_model_glb.c src/render/skinned_pose.c \
              src/render/glb.c src/render/stb_image.c src/core/json.c

# What a swing hits: one hit per target, nothing outside the live window, and
# no tunnelling at speed - three things invisible from the player's chair. See
# tools/melee_probe.c.
MELEE_PROBE := bin/melee_probe
MELEE_PROBE_SRCS := tools/melee_probe.c src/gameplay/melee_swing.c \
              src/gameplay/weapon.c src/gameplay/traversal.c \
              src/render/viewmodel_neck.c \
              src/gameplay/action_track.c src/gameplay/third_person_camera.c \
              src/gameplay/combatant.c src/gameplay/damage_number.c \
              src/gameplay/spawn_select.c src/gameplay/arena.c \
              src/gameplay/weapon_specs.c src/gameplay/weapon_spec.c \
              src/gameplay/viewmodel_pose.c src/gameplay/sights.c

# Health, death and respawn, checked without a window for the same reason the
# other two are - and with more need, because nothing in the world can shoot the
# player yet, so this file and one debug key are the only things that run the
# loop at all. See tools/death_probe.c.
DEATH_PROBE := bin/death_probe
DEATH_PROBE_SRCS := tools/death_probe.c src/gameplay/combatant.c \
              src/gameplay/damage_number.c src/gameplay/spawn_select.c \
              src/gameplay/player_combatant.c src/gameplay/arena.c \
              src/gameplay/player_controller.c src/gameplay/player_locomotion.c \
              src/gameplay/player_slide.c src/gameplay/player_wallrun.c \
              src/gameplay/player_wallclimb.c src/gameplay/player_grapple.c \
              src/gameplay/player_mantle.c src/gameplay/traversal.c

# The weapon specs, checked without a window: that every weapon says what kind
# it is and carries the one class half that kind means, that its mesh loads,
# and that every part name it states exists on that mesh. See
# tools/weapon_spec_probe.c for why each of those is worth a check.
SPEC_PROBE := bin/weapon_spec_probe
SPEC_PROBE_SRCS := tools/weapon_spec_probe.c src/gameplay/weapon_spec.c \
              src/gameplay/weapon_registry.c \
              src/gameplay/weapon_specs.c src/gameplay/viewmodel_pose.c \
              src/gameplay/action_track.c src/gameplay/sights.c \
              src/render/static_model_glb.c src/render/glb.c \
              src/render/stb_image.c src/core/json.c

.PHONY: meleeprobe sheathprobe all build clean run probe bodyprobe deathprobe specprobe

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

# Run from the project root: it opens models/animations/UAL1_Standard.glb by a
# relative path, the same one the game uses.
probe: $(PROBE_SRCS)
	@mkdir -p bin
	$(PROBE_CC) -Wall -Wextra -std=c11 -O1 $(CPPFLAGS) $(PROBE_SRCS) \
	    -o $(PROBE) -lm
	./$(PROBE)

# Run from the project root, for the same relative model path the game uses.
meleeprobe: $(MELEE_PROBE_SRCS)
	@mkdir -p bin
	$(PROBE_CC) -Wall -Wextra -std=c11 -O1 $(CPPFLAGS) $(MELEE_PROBE_SRCS) \
	    -o $(MELEE_PROBE) -lm
	./$(MELEE_PROBE)

sheathprobe: $(SHEATH_PROBE_SRCS)
	@mkdir -p bin
	$(PROBE_CC) -Wall -Wextra -std=c11 -O1 $(CPPFLAGS) $(SHEATH_PROBE_SRCS) \
	    -o $(SHEATH_PROBE) -lm
	./$(SHEATH_PROBE)

bodyprobe: $(BODY_PROBE_SRCS)
	@mkdir -p bin
	$(PROBE_CC) -Wall -Wextra -std=c11 -O1 $(CPPFLAGS) $(BODY_PROBE_SRCS) \
	    -o $(BODY_PROBE) -lm
	./$(BODY_PROBE)

# Run from the project root, like the others.
specprobe: $(SPEC_PROBE_SRCS)
	@mkdir -p bin
	$(PROBE_CC) -Wall -Wextra -std=c11 -O1 $(CPPFLAGS) $(SPEC_PROBE_SRCS) \
	    -o $(SPEC_PROBE) -lm
	./$(SPEC_PROBE)

# Run from the project root, like the other two.
deathprobe: $(DEATH_PROBE_SRCS)
	@mkdir -p bin
	$(PROBE_CC) -Wall -Wextra -std=c11 -O1 $(CPPFLAGS) $(DEATH_PROBE_SRCS) \
	    -o $(DEATH_PROBE) -lm
	./$(DEATH_PROBE)

clean:
	rm -rf obj $(TARGET) $(PROBE) $(BODY_PROBE) $(DEATH_PROBE) \
	    $(SPEC_PROBE) $(SHEATH_PROBE) $(MELEE_PROBE) $(BUILD_DIR)

-include $(DEPS)
