/*
 * ALTTP voxel-orbit experiment. The renderer is presentation-only; this file
 * owns Zelda-specific visibility, height policy, and live camera controls.
 */
#include "zelda_voxel.h"

#include "common_rtl.h"
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
static float s_pitch = 38.0f, s_yaw = -22.0f, s_roll;
static float s_distance = 285.0f;
static float s_render_pitch = 38.0f, s_render_yaw = -22.0f, s_render_roll;
static float s_render_distance = 285.0f;
static float s_right_x, s_right_y;

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
          "[Voxel] ALTTP orbit experiment %s "
          "(Numpad 0 toggles, right stick rotates)\n",
          s_enabled ? "enabled" : "disabled");
}

static int gameplay_visible(void) {
  uint8_t module = g_ram[0x10];
  return module == 7 || module == 9;
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

static float zelda_cell_height(const uint32_t *pixels, int stride,
                               int size, int cell_x, int cell_y,
                               void *user) {
  uint8_t module = g_ram[0x10];
  (void)user;
  if (module == 7) {
    uint16_t scroll_x = *(uint16_t *)(g_ram + 0x00e2);
    uint16_t scroll_y = *(uint16_t *)(g_ram + 0x00e8);
    uint16_t world_x = (uint16_t)(scroll_x + cell_x * size + size / 2);
    uint16_t world_y = (uint16_t)(
        scroll_y + ZELDA_VOXEL_SOURCE_Y + cell_y * size + size / 2);
    int floor_offset = g_ram[0x00ee] ? 0x1000 : 0;
    int index = floor_offset + ((world_x & 0x1f8) >> 3) +
                ((world_y & 0x1f8) << 3);
    uint8_t attribute = g_ram[0xfe00 + index];
    if (attribute == 0x20 ||
        (attribute >= 0xb0 && attribute <= 0xbd)) return -4.0f;
    if (attribute == 0x08 || attribute == 0x09) return -2.0f;
    if (dungeon_attribute_is_wall(attribute)) return 12.0f;
  }
  return pixel_material_height(pixels, stride, size);
}

static void update_camera(void) {
  s_yaw += stick_curve(s_right_x) * 2.8f;
  if (s_yaw > 180.0f) s_yaw -= 360.0f;
  if (s_yaw < -180.0f) s_yaw += 360.0f;
  s_pitch = clamp_float(
      s_pitch - stick_curve(s_right_y) * 2.1f, 8.0f, 82.0f);
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
    if (event->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) s_right_x = value;
    else if (event->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) s_right_y = value;
    return;
  }
  if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
    s_right_x = s_right_y = 0.0f;
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
      s_pitch = (float)clamp_int((int)s_pitch + 5, 8, 82);
      changed = 1; break;
    case SDL_SCANCODE_KP_2:
      s_pitch = (float)clamp_int((int)s_pitch - 5, 8, 82);
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
      s_pitch = 38.0f; s_yaw = -22.0f; s_roll = 0.0f;
      s_distance = 285.0f; changed = 1; break;
    default: break;
  }
  if (changed) {
    while (s_yaw > 180.0f) s_yaw -= 360.0f;
    while (s_yaw < -180.0f) s_yaw += 360.0f;
    fprintf(stderr,
            "[Voxel] orbit view=%s pitch=%.0f yaw=%.0f roll=%.0f "
            "distance=%.0f\n",
            s_view_enabled ? "on" : "off", s_pitch, s_yaw, s_roll,
            s_distance);
  }
}

uint32_t ZeldaVoxelRemapInput(uint32_t input) {
  initialize_once();
  return input;
}

void ZeldaVoxelPostRender(uint8_t *pixels, size_t pitch,
                          int width, int height) {
  SnesVoxelScene scene;
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
  scene.elevation_degrees = s_render_pitch;
  scene.yaw_degrees = s_render_yaw;
  scene.roll_degrees = s_render_roll;
  scene.camera_distance = s_render_distance;
  scene.camera_focal_scale = 0.90f;
  scene.camera_center_y = 0.61f;
  scene.preserve_top_rows = ZELDA_VOXEL_SOURCE_Y;
  scene.sky_top = g_ram[0x10] == 7 ? 0xff10151fu : 0xff243b5au;
  scene.sky_bottom = g_ram[0x10] == 7 ? 0xff4b5360u : 0xff8fb6c6u;
  snes_voxel_render(&scene);
}
