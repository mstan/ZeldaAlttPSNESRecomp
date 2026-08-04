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
static int s_link_overlay_bound;
static int8_t s_overworld_height[64 * 64];
static int8_t s_overworld_candidate[64 * 64];
static uint8_t s_overworld_votes[64 * 64];
static uint16_t s_overworld_area = 0xffff;

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

static float pixel_material_height(const uint32_t *pixels, int stride,
                                   int size) {
  int dark = 0, green = 0, edges = 0, luminance_sum = 0;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      uint32_t color = pixels[y * stride + x];
      int red = (color >> 16) & 0xff;
      int g = (color >> 8) & 0xff;
      int blue = color & 0xff;
      int luminance = (red * 3 + g * 5 + blue * 2) / 10;
      luminance_sum += luminance;
      if (luminance < 42) dark++;
      if (g > red + 18 && g > blue + 12) green++;
      if (x > 0) {
        uint32_t left = pixels[y * stride + x - 1];
        int left_luma = ((((left >> 16) & 0xff) * 3) +
                         (((left >> 8) & 0xff) * 5) +
                         ((left & 0xff) * 2)) / 10;
        if (abs(luminance - left_luma) > 44) edges++;
      }
      if (y > 0) {
        uint32_t above = pixels[(y - 1) * stride + x];
        int above_luma = ((((above >> 16) & 0xff) * 3) +
                          (((above >> 8) & 0xff) * 5) +
                          ((above & 0xff) * 2)) / 10;
        if (abs(luminance - above_luma) > 44) edges++;
      }
    }
  }
  if (dark > size * size * 3 / 4) return -3.0f;
  if (green > size * size / 2 && edges > size) return 10.0f;
  if (edges > size * 3) return 6.0f;
  if (luminance_sum / (size * size) < 62) return 3.0f;
  return 0.0f;
}

static float stable_overworld_height(const uint32_t *pixels, int stride,
                                     int size, uint16_t world_x,
                                     uint16_t world_y) {
  uint16_t area = read_wram16(0x8a);
  int index = ((world_y & 0x1f8) >> 3) * 64 +
              ((world_x & 0x1f8) >> 3);
  int8_t candidate =
      (int8_t)pixel_material_height(pixels, stride, size);

  if (area != s_overworld_area) {
    memset(s_overworld_height, 0x80, sizeof(s_overworld_height));
    memset(s_overworld_votes, 0, sizeof(s_overworld_votes));
    s_overworld_area = area;
  }
  if (s_overworld_height[index] != INT8_MIN)
    return (float)s_overworld_height[index];

  if (!s_overworld_votes[index] ||
      s_overworld_candidate[index] != candidate) {
    s_overworld_candidate[index] = candidate;
    s_overworld_votes[index] = 1;
  } else if (s_overworld_votes[index] < 3) {
    s_overworld_votes[index]++;
  }
  if (s_overworld_votes[index] >= 3) {
    s_overworld_height[index] = candidate;
    return (float)candidate;
  }
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
    return stable_overworld_height(
        pixels, stride, size, world_x, world_y);
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
    return;
  }

  if (!s_link_overlay_bound) {
    s_link_overlay_bound = PpuBindOverlaySurface(
        g_ppu, kPpuOverlaySource_Obj, (uint8_t *)s_link_overlay,
        256 * sizeof(uint32_t));
  }
  if (!s_link_overlay_bound) return;

  link_x = (int16_t)(uint16_t)(
      read_wram16(0x22) - read_wram16(0xe2));
  link_y = (int16_t)(uint16_t)(
      read_wram16(0x20) - read_wram16(0xe8));

  /* ALTTP moves Link's component sprites between OAM regions as equipment and
   * animation change, so filter spatially across the complete OAM table.
   * Only pixels inside his tight live body rectangle are omitted; weapon
   * pixels extending beyond it, enemies, rain, and other effects remain. */
  if (PpuSetOverlayCapture(g_ppu, kPpuOverlaySource_Obj,
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
  scene.camera_eye_y = 8.0f;
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
  scene.preserve_top_rows = ZELDA_VOXEL_SOURCE_Y;
  scene.sky_top = g_ram[0x10] == 7 ? 0xff10151fu : 0xff243b5au;
  scene.sky_bottom = g_ram[0x10] == 7 ? 0xff4b5360u : 0xff8fb6c6u;
  snes_voxel_render(&scene);
}
