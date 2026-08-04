/*
 * ALTTP first-person voxel experiment. The renderer is presentation-only;
 * this file also maps view-relative movement back onto Zelda's four native
 * directional input bits before each emulated frame.
 */
#include "zelda_voxel.h"

#include "common_rtl.h"
#include "snes/ppu.h"
#include "voxel_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZELDA_VOXEL_SOURCE_Y 32
#define ZELDA_VOXEL_SOURCE_HEIGHT 192

static int s_initialized;
static int s_enabled = 1;
static int s_view_enabled = 1;
static int s_heading_initialized;
static int s_last_mapped_direction;
static float s_heading = -90.0f;
static float s_pitch, s_yaw, s_roll;
static float s_distance = 285.0f;
static float s_render_pitch, s_render_yaw, s_render_roll;
static float s_render_distance = 285.0f;
static float s_left_x, s_left_y, s_right_x, s_right_y;
static uint32_t s_link_overlay[256 * 240];
static uint32_t s_hud_overlay[256 * 240];
static int s_link_overlay_bound;
static int s_hud_overlay_bound;

static int clamp_int(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

static float clamp_float(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

static uint16_t read_wram16(unsigned address) {
  return (uint16_t)(g_ram[address] |
                    ((uint16_t)g_ram[address + 1] << 8));
}

static float ease(float current, float target) {
  return current + (target - current) * 0.22f;
}

static float stick_curve(float value) {
  float magnitude = fabsf(value), scaled;
  if (magnitude <= 0.18f) return 0.0f;
  scaled = (magnitude - 0.18f) / 0.82f;
  scaled *= scaled;
  return value < 0.0f ? -scaled : scaled;
}

static void initialize_once(void) {
  const char *value;
  if (s_initialized) return;
  s_initialized = 1;
  value = getenv("SNESRECOMP_VOXEL3D");
  if (value && (!value[0] || value[0] == '0')) s_enabled = 0;
  s_view_enabled = s_enabled;
  fprintf(stderr,
          "[Voxel] ALTTP first-person experiment %s "
          "(right stick looks, movement is camera-relative)\n",
          s_enabled ? "enabled" : "disabled");
}

static int gameplay_visible(void) {
  uint8_t module = g_ram[0x10];
  return module == 7 || module == 9;
}

static float wrap_heading(float heading) {
  while (heading > 180.0f) heading -= 360.0f;
  while (heading < -180.0f) heading += 360.0f;
  return heading;
}

static void initialize_heading(void) {
  if (s_heading_initialized) return;
  switch (g_ram[0x2f] & 6) {
    case 0: s_heading = -90.0f; break;
    case 2: s_heading = 90.0f; break;
    case 4: s_heading = 180.0f; break;
    default: s_heading = 0.0f; break;
  }
  s_heading_initialized = 1;
}

static int dungeon_attribute_is_wall(uint8_t attribute) {
  return (attribute >= 0x01 && attribute <= 0x04) ||
         attribute == 0x26 || attribute == 0x27 ||
         attribute == 0x43 || attribute == 0x44 ||
         attribute == 0x46 || attribute == 0x57 ||
         (attribute >= 0x50 && attribute <= 0x56) ||
         (attribute >= 0x58 && attribute <= 0x5d) ||
         attribute == 0x63 || attribute == 0x67 ||
         (attribute >= 0x70 && attribute <= 0xaf);
}

static uint16_t read_rom16(uint32_t address) {
  const uint8_t *data = RomPtr(address);
  return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

/*
 * This is ALTTP's live Overworld_GetTileAttributeAtLocation lookup.  It
 * resolves the current mutable Map16 tile in WRAM down to the exact 8x8
 * collision attribute, so weather and animated palette pixels never affect
 * geometry.
 */
static uint8_t overworld_tile_attribute(uint16_t world_x,
                                        uint16_t world_y) {
  uint16_t tile_x = world_x >> 3;
  uint16_t offset =
      (uint16_t)(((world_y - read_wram16(0x0708)) &
                  read_wram16(0x070a)) * 8);
  uint16_t map16, map8;
  offset |= (uint16_t)((tile_x - read_wram16(0x070c)) &
                       read_wram16(0x070e));
  map16 = read_wram16(0x2000 + ((offset >> 1) << 1));
  if (map16 >= 3752) return 0;
  map8 = read_rom16(0x8f8000u +
                    (uint32_t)(map16 * 4 +
                               ((world_y & 8) >> 2) +
                               (tile_x & 1)) * 2);
  return RomPtr(0x8e9459u)[map8 & 0x1ff];
}

static int overworld_attribute_is_solid(uint8_t attribute) {
  return (attribute >= 0x01 && attribute <= 0x03) ||
         attribute == 0x26 || attribute == 0x27 ||
         attribute == 0x42 || attribute == 0x43 ||
         attribute == 0x46 ||
         (attribute >= 0x50 && attribute <= 0x57);
}

static float overworld_attribute_height(uint8_t attribute) {
  if (attribute == 0x20 ||
      (attribute >= 0xb0 && attribute <= 0xbd))
    return -8.0f;
  if (attribute == 0x08 || attribute == 0x0b)
    return -5.0f;
  if (attribute == 0x09)
    return -2.0f;

  /* Diagonal slopes, exterior stairs, and the walkable interruption in a
   * ledge form the intermediate tread between ground and a full plateau. */
  if ((attribute >= 0x10 && attribute <= 0x13) ||
      (attribute >= 0x18 && attribute <= 0x1f) ||
      attribute == 0x22 ||
      (attribute >= 0x30 && attribute <= 0x3f))
    return 16.0f;

  /* Ledge faces are the two-Link-tall elevation break visible in the
   * top-down art, rather than a knee-high material bump. */
  if (attribute >= 0x28 && attribute <= 0x2f)
    return 32.0f;
  if (overworld_attribute_is_solid(attribute))
    return 36.0f;
  return 0.0f;
}

static float zelda_cell_height(const uint32_t *pixels, int stride,
                               int size, int cell_x, int cell_y,
                               void *user) {
  uint8_t module = g_ram[0x10];
  uint16_t scroll_x = read_wram16(0x00e2);
  uint16_t scroll_y = read_wram16(0x00e8);
  uint16_t world_x =
      (uint16_t)(scroll_x + cell_x * size + size / 2);
  uint16_t world_y = (uint16_t)(
      scroll_y + ZELDA_VOXEL_SOURCE_Y + cell_y * size + size / 2);
  (void)user;
  if (module == 7) {
    int floor_offset = g_ram[0x00ee] ? 0x1000 : 0;
    int index = floor_offset + ((world_x & 0x1f8) >> 3) +
                ((world_y & 0x1f8) << 3);
    uint8_t attribute = g_ram[0xfe00 + index];
    if (attribute == 0x20 ||
        (attribute >= 0xb0 && attribute <= 0xbd)) return -4.0f;
    if (attribute == 0x08 || attribute == 0x09) return -2.0f;
    if (dungeon_attribute_is_wall(attribute)) return 12.0f;
    return 0.0f;
  }
  if (module == 9)
    return overworld_attribute_height(
        overworld_tile_attribute(world_x, world_y));
  return 0.0f;
}

static void update_camera(void) {
  if (s_enabled && s_view_enabled && gameplay_visible()) {
    initialize_heading();
    s_heading = wrap_heading(
        s_heading + stick_curve(s_right_x) * 3.5f);
    s_pitch = clamp_float(
        s_pitch - stick_curve(s_right_y) * 2.4f, -50.0f, 50.0f);
  }
  s_render_pitch = ease(s_render_pitch, s_pitch);
  s_render_yaw = ease(s_render_yaw, s_yaw);
  s_render_roll = ease(s_render_roll, s_roll);
  s_render_distance = ease(s_render_distance, s_distance);
}

void ZeldaVoxelHandleEvent(const SDL_Event *event) {
  SDL_Scancode key;
  int changed = 0;
  initialize_once();
  if (!s_enabled || !event) return;
  if (event->type == SDL_CONTROLLERAXISMOTION) {
    float value = event->caxis.value < 0
                      ? (float)event->caxis.value / 32768.0f
                      : (float)event->caxis.value / 32767.0f;
    if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) s_left_x = value;
    else if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) s_left_y = value;
    else if (event->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) s_right_x = value;
    else if (event->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) s_right_y = value;
    return;
  }
  if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
    s_left_x = s_left_y = s_right_x = s_right_y = 0.0f;
    return;
  }
  if (event->type != SDL_KEYDOWN) return;
  key = event->key.keysym.scancode;
  if (event->key.repeat &&
      (key == SDL_SCANCODE_KP_0 || key == SDL_SCANCODE_KP_5)) return;
  switch (key) {
    case SDL_SCANCODE_KP_0:
      s_view_enabled = !s_view_enabled; changed = 1; break;
    case SDL_SCANCODE_KP_8:
      s_pitch = (float)clamp_int((int)s_pitch + 5, -50, 50);
      changed = 1; break;
    case SDL_SCANCODE_KP_2:
      s_pitch = (float)clamp_int((int)s_pitch - 5, -50, 50);
      changed = 1; break;
    case SDL_SCANCODE_KP_4: s_yaw -= 5.0f; changed = 1; break;
    case SDL_SCANCODE_KP_6: s_yaw += 5.0f; changed = 1; break;
    case SDL_SCANCODE_KP_7:
      s_roll = clamp_float(s_roll - 5.0f, -45.0f, 45.0f);
      changed = 1; break;
    case SDL_SCANCODE_KP_9:
      s_roll = clamp_float(s_roll + 5.0f, -45.0f, 45.0f);
      changed = 1; break;
    case SDL_SCANCODE_KP_PLUS:
      s_distance = clamp_float(s_distance - 12.0f, 150.0f, 430.0f);
      changed = 1; break;
    case SDL_SCANCODE_KP_MINUS:
      s_distance = clamp_float(s_distance + 12.0f, 150.0f, 430.0f);
      changed = 1; break;
    case SDL_SCANCODE_KP_5:
      s_pitch = 0.0f; s_yaw = 0.0f; s_roll = 0.0f;
      s_distance = 285.0f; s_heading_initialized = 0;
      changed = 1; break;
    default: break;
  }
  if (changed) {
    while (s_yaw > 180.0f) s_yaw -= 360.0f;
    while (s_yaw < -180.0f) s_yaw += 360.0f;
    fprintf(stderr,
            "[Voxel] first-person view=%s pitch=%.0f yaw-offset=%.0f "
            "roll=%.0f "
            "distance=%.0f\n",
            s_view_enabled ? "on" : "off", s_pitch, s_yaw, s_roll,
            s_distance);
  }
}

uint32_t ZeldaVoxelRemapInput(uint32_t input) {
  float strafe = 0.0f, forward = 0.0f;
  float magnitude, heading, world_x, world_z;
  uint32_t directions;
  int mapped = 0;
  initialize_once();
  if (!s_enabled || !s_view_enabled || !gameplay_visible()) {
    s_last_mapped_direction = 0;
    return input;
  }
  initialize_heading();
  directions = input & 0xf0;
  magnitude = sqrtf(s_left_x * s_left_x + s_left_y * s_left_y);
  if (magnitude > 0.25f) {
    float strength = (magnitude - 0.25f) / 0.75f;
    if (strength > 1.0f) strength = 1.0f;
    strafe = s_left_x / magnitude * strength;
    forward = -s_left_y / magnitude * strength;
  } else {
    strafe = ((directions & 0x80) ? 1.0f : 0.0f) -
             ((directions & 0x40) ? 1.0f : 0.0f);
    forward = ((directions & 0x10) ? 1.0f : 0.0f) -
              ((directions & 0x20) ? 1.0f : 0.0f);
  }
  if (fabsf(strafe) > 0.01f || fabsf(forward) > 0.01f) {
    float abs_x, abs_z;
    heading = (s_heading + s_render_yaw) *
              3.14159265358979323846f / 180.0f;
    world_x = cosf(heading) * forward - sinf(heading) * strafe;
    world_z = sinf(heading) * forward + cosf(heading) * strafe;
    abs_x = fabsf(world_x);
    abs_z = fabsf(world_z);
    if (fabsf(abs_x - abs_z) < 0.10f &&
        s_last_mapped_direction != 0) {
      if (s_last_mapped_direction == 0x40 ||
          s_last_mapped_direction == 0x80)
        mapped = world_x >= 0.0f ? 0x80 : 0x40;
      else
        mapped = world_z >= 0.0f ? 0x20 : 0x10;
    } else if (abs_x >= abs_z) {
      mapped = world_x >= 0.0f ? 0x80 : 0x40;
    } else {
      mapped = world_z >= 0.0f ? 0x20 : 0x10;
    }
  }
  s_last_mapped_direction = mapped;
  return (input & ~0xf0u) | (uint32_t)mapped;
}

void ZeldaVoxelConfigurePpu(void) {
  int link_x, link_y;
  initialize_once();
  if (!g_ppu) return;

  PpuClearOverlayCaptures(g_ppu);
  if (!s_enabled || !s_view_enabled || !gameplay_visible()) {
    if (s_link_overlay_bound) {
      PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Obj, NULL, 0);
      s_link_overlay_bound = 0;
    }
    if (s_hud_overlay_bound) {
      PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Bg3, NULL, 0);
      s_hud_overlay_bound = 0;
    }
    return;
  }

  if (!s_link_overlay_bound) {
    s_link_overlay_bound = PpuBindOverlaySurface(
        g_ppu, kPpuOverlaySource_Obj, (uint8_t *)s_link_overlay,
        256 * sizeof(uint32_t));
  }
  if (!s_hud_overlay_bound) {
    s_hud_overlay_bound = PpuBindOverlaySurface(
        g_ppu, kPpuOverlaySource_Bg3, (uint8_t *)s_hud_overlay,
        256 * sizeof(uint32_t));
  }
  if (s_hud_overlay_bound)
    PpuSetOverlayCapture(g_ppu, kPpuOverlaySource_Bg3,
                         0, 0, 256, ZELDA_VOXEL_SOURCE_Y, 0);

  link_x = (int16_t)(uint16_t)(
      read_wram16(0x22) - read_wram16(0xe2));
  link_y = (int16_t)(uint16_t)(
      read_wram16(0x20) - read_wram16(0xe8));

  /* ALTTP moves Link's component sprites between OAM regions as equipment and
   * animation change, so filter spatially across the complete OAM table.
   * Only pixels inside his tight live body rectangle are omitted; weapon
   * pixels extending beyond it, enemies, rain, and other effects remain. */
  if (s_link_overlay_bound &&
      PpuSetOverlayCapture(g_ppu, kPpuOverlaySource_Obj,
                           link_x - 12, link_y - 20, 40, 48,
                           kPpuOverlayFlag_RemoveFromGame))
    PpuSetOverlayOamRange(g_ppu, 0, 128);
}

void ZeldaVoxelPostRender(uint8_t *pixels, size_t pitch,
                          int width, int height) {
  SnesVoxelScene scene;
  float heading, look_pitch, eye_x, eye_z;
  int source_x;
  initialize_once();
  update_camera();
  if (!s_enabled || !s_view_enabled || !gameplay_visible() ||
      !pixels || width < 256 || height < 224) return;
  source_x = (width - 256) / 2;
  memset(&scene, 0, sizeof(scene));
  scene.framebuffer = (uint32_t *)pixels;
  scene.framebuffer_stride = (int)(pitch / sizeof(uint32_t));
  scene.output_width = width;
  scene.output_height = height;
  scene.source_x = source_x;
  scene.source_y = ZELDA_VOXEL_SOURCE_Y;
  scene.source_width = 256;
  scene.source_height = ZELDA_VOXEL_SOURCE_HEIGHT;
  scene.cell_size = 8;
  scene.cell_height = zelda_cell_height;
  scene.roll_degrees = s_render_roll;
  initialize_heading();
  heading = (s_heading + s_render_yaw) *
            3.14159265358979323846f / 180.0f;
  look_pitch = s_render_pitch *
               3.14159265358979323846f / 180.0f;
  eye_x = (float)(int16_t)(uint16_t)(
              read_wram16(0x22) - read_wram16(0xe2)) + 8.0f;
  eye_z = (float)(int16_t)(uint16_t)(
              read_wram16(0x20) - read_wram16(0xe8)) -
          ZELDA_VOXEL_SOURCE_Y + 12.0f;
  eye_x = clamp_float(eye_x, 4.0f, 252.0f);
  eye_z = clamp_float(eye_z, 4.0f, 188.0f);
  scene.use_camera_pose = 1;
  scene.camera_eye_x = eye_x + cosf(heading) * 2.0f;
  scene.camera_eye_y = 12.0f;
  scene.camera_eye_z = eye_z + sinf(heading) * 2.0f;
  scene.camera_look_at_x =
      scene.camera_eye_x + cosf(heading) * cosf(look_pitch) * 128.0f;
  scene.camera_look_at_y =
      scene.camera_eye_y + sinf(look_pitch) * 128.0f;
  scene.camera_look_at_z =
      scene.camera_eye_z + sinf(heading) * cosf(look_pitch) * 128.0f;
  scene.camera_focal_scale =
      clamp_float(0.78f * 285.0f / s_render_distance, 0.48f, 1.25f);
  scene.camera_center_y = 0.58f;
  scene.preserve_top_rows = 0;
  scene.sky_top = g_ram[0x10] == 7 ? 0xff10151fu : 0xff243b5au;
  scene.sky_bottom = g_ram[0x10] == 7 ? 0xff4b5360u : 0xff8fb6c6u;
  if (snes_voxel_render(&scene) && s_hud_overlay_bound) {
    uint32_t *frame = (uint32_t *)pixels;
    for (int y = 0; y < ZELDA_VOXEL_SOURCE_Y; y++) {
      for (int x = 0; x < 256; x++) {
        uint32_t color = s_hud_overlay[y * 256 + x];
        if (color)
          frame[y * scene.framebuffer_stride + source_x + x] = color;
      }
    }
  }
}
