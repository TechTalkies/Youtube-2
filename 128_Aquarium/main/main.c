#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "lvgl.h" // Added LVGL

static const char *TAG = "aquarium_lvgl";

// --- Hardware Pin Mapping ---
#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

#define LCD_PIN_NUM_SCLK 7
#define LCD_PIN_NUM_MOSI 9
#define LCD_PIN_NUM_MISO -1
#define LCD_PIN_NUM_CS 2
#define LCD_PIN_NUM_DC 4
#define LCD_PIN_NUM_BL 43
#define LCD_PIN_NUM_RST -1

// --- Panel colour tuning (if colours still look off, flip these two) ---
#define LCD_INVERT_COLOR true
#define LCD_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR

#define I2C_MASTER_SCL_IO 6
#define I2C_MASTER_SDA_IO 5
#define TOUCH_INT_PIN 44
#define TOUCH_I2C_ADDR 0x2E

// --- Aquarium Settings ---
#define NUM_FISH 48
#define MAX_ACTIVE_SCHOOLS 3
#define FISH_SIZE 8

#define NUM_KELP 7
#define KELP_SEGMENTS 6
#define FISH_DIRTY_TILE_SIZE 48
#define FISH_DIRTY_TILE_COUNT 5

// --- Light Ray Settings ---
static lv_obj_t *light_rays_obj;
static float light_ray_phase = 0.0f;
static lv_img_dsc_t ray_dsc;

typedef struct
{
    lv_obj_t *obj;
    lv_point_t points[KELP_SEGMENTS];
    float base_x;
    float phase;
    float sway_speed;
    float height_scale;

    // New variety features
    lv_obj_t *branch_obj;
    lv_point_t branch_points[3];
    int branch_idx;   // Which main segment the branch sprouts from
    float branch_dir; // 1.0f for right, -1.0f for left

    lv_obj_t *bulb_obj; // Small colored tip
} Kelp;

Kelp forest[NUM_KELP];

typedef enum
{
    SPECIES_TETRA,
    SPECIES_GOLDFISH,
    SPECIES_DANIO,
    SPECIES_NEON,
    SPECIES_PLATY,
    SPECIES_MOLLIE,
    SPECIES_JELLYFISH, // NEW
    SPECIES_MANTA,     // NEW
    SPECIES_COUNT
} FishSpecies;

// Each species has one more fish than the original school size.
static const int SCHOOL_SIZE[SPECIES_COUNT] = {9, 4, 7, 11, 4, 6, 4, 1};
static const int SCHOOL_FIRST[SPECIES_COUNT] = {0, 9, 13, 20, 31, 35, 41, 46};
_Static_assert(46 + 2 == NUM_FISH, "SCHOOL_SIZE must add up to NUM_FISH");

typedef struct
{
    uint32_t color;          // LVGL color hex
    int size;                // Fish width/height
    float height_ratio;      // 1.0 = circle, <1.0 = flattened, >1.0 = elongated
    float max_speed;         // Top speed limit
    float min_speed;         // Minimum speed limit
    float separation_dist;   // How hard they push away from others
    float neighbor_dist;     // How far they look for friends to school with
    float cohesion_weight;   // Desire to go to the center of the school
    float alignment_weight;  // Desire to match speed/direction of the school
    float separation_weight; // Desire to avoid crashing
} SpeciesConfig;

// Define the struct for your sprites
typedef struct
{
    int width;
    int height;
    const char *name;
    const uint8_t *pixels_f1; // Frame 1
    const uint8_t *pixels_f2; // Frame 2 (Set to NULL if not animated)
    uint32_t palette[6];
} FishSprite;

/* -------------------------------------------------------
- GOLDFISH: Large, flowing fins, deep orange shading
- Size: 32 x 22
- Colors: Trans, Black, Dark Orange, Orange, Yellow, White
------------------------------------------------------- */

static const uint8_t goldfish_pixels[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 4, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 4, 4, 4, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 4, 3, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 3, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 1, 3, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 3, 1, 0, 0, 0, 0,
    0, 0, 1, 3, 3, 3, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 5, 1, 1, 5, 1, 0, 0, 0, 0,
    0, 1, 3, 3, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 3, 1, 0, 0, 0, 0,
    0, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 5, 5, 3, 3, 3, 1, 0, 0, 0, 0, 0,
    0, 0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 5, 5, 5, 5, 5, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 5, 5, 5, 5, 5, 5, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 5, 5, 5, 5, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 5, 5, 1, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 1, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 1, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* -------------------------------------------------------
- TETRA: Bold red stripe, silver belly
- Size: 26 x 14
- Colors: Trans, Black, Dark Blue, Red, Silver/Blue, White
------------------------------------------------------- */

static const uint8_t tetra_pixels[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 2, 2, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 1, 0, 0, 0, 0,
    1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5, 1, 0, 0, 0,
    1, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 1, 1, 5, 1, 0, 0,
    1, 2, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 1, 0, 0,
    0, 1, 2, 2, 2, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 1, 0, 0, 0,
    0, 0, 1, 1, 1, 1, 2, 2, 2, 5, 5, 5, 5, 5, 5, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 5, 5, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* -------------------------------------------------------
- DANIO: Horizontal torpedo stripes
- Size: 28 x 12
- Colors: Trans, Black, Dark Green, Green, Yellow, White
------------------------------------------------------- */

static const uint8_t danio_pixels[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1, 1, 3, 3, 3, 3, 3, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 3, 3, 3, 4, 4, 4, 4, 4, 4, 3, 3, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 3, 3, 4, 4, 4, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 3, 3, 1, 1, 0, 0, 0, 0, 0,
    1, 4, 4, 4, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2, 4, 5, 5, 3, 1, 0, 0, 0, 0,
    1, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 5, 1, 1, 5, 1, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2, 2, 5, 5, 5, 5, 3, 1, 0, 0, 0, 0,
    0, 1, 1, 1, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 5, 5, 3, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* -------------------------------------------------------
- NEON: Tiny, bright cyan/pink dash
- Size: 20 x 10
- Colors: Trans, Black, Dark Blue, Cyan, Pink, White
------------------------------------------------------- */

static const uint8_t neon_pixels[] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 3, 3, 3, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 0, 0, 0, 0, 0, 0,
    1, 3, 3, 3, 4, 4, 4, 4, 4, 4, 3, 3, 5, 5, 1, 0, 0, 0, 0, 0,
    1, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 5, 1, 1, 5, 1, 0, 0, 0, 0,
    0, 1, 1, 2, 2, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 1, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 2, 2, 2, 5, 5, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* -------------------------------------------------------
- PLATY: Deep pink body
- Size: 26 x 16
- Colors: Trans, Black, Dark Red, Hot Pink, Light Pink, White
------------------------------------------------------- */

static const uint8_t platy_pixels[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 4, 4, 4, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 3, 4, 4, 4, 4, 4, 4, 3, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 1, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 3, 1, 0, 0, 0, 0, 0,
    1, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 1, 1, 5, 1, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 3, 1, 0, 0, 0, 0, 0,
    0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 2, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 5, 5, 5, 5, 5, 5, 5, 5, 2, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 5, 5, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* -------------------------------------------------------
- MOLLIE: Pointed fins, dark purple
- Size: 28 x 14
- Colors: Trans, Black, Dark Purple, Purple, Violet, White
------------------------------------------------------- */

static const uint8_t mollie_pixels[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 3, 4, 4, 4, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 3, 3, 4, 4, 4, 4, 4, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 3, 1, 1, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 1, 1, 5, 1, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 3, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 2, 2, 3, 3, 3, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 5, 5, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* -------------------------------------------------------
- JELLYFISH: Pulsating bell with trailing tentacles
- Size: 18 x 15 (Animated: 2 Frames)
- Colors: Trans, Black, Dark Purple, Blue, Light Blue, Cyan
------------------------------------------------------- */
static const uint8_t jelly_f1[] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0, 0, 0,
    0, 0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 0, 0,
    0, 1, 2, 3, 4, 4, 5, 5, 4, 4, 5, 5, 4, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 4, 5, 5, 4, 4, 5, 5, 4, 4, 3, 2, 1, 0,
    0, 0, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 0, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 5, 1, 0, 1, 5, 1, 0, 1, 5, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 4, 1, 0, 1, 4, 1, 0, 1, 4, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 3, 1, 0, 1, 3, 1, 0, 1, 3, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0};

static const uint8_t jelly_f2[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0, 0, 0,
    0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0, 0,
    0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 0,
    1, 2, 3, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 3, 2, 1,
    1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 3, 2, 1,
    1, 2, 3, 4, 4, 5, 5, 4, 4, 4, 4, 5, 5, 4, 4, 3, 2, 1,
    0, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 1, 5, 5, 1, 0, 0, 1, 5, 5, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 4, 4, 1, 0, 0, 1, 4, 4, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 3, 1, 0, 0, 1, 3, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};

/* Manta viewed from above: narrow tail to the left, swept wings, head to the right. */
static uint8_t manta_pixels_f1[34 * 28];
static uint8_t manta_pixels_f2[34 * 28];

static void create_manta_top_view(uint8_t *pixels, bool wings_wide)
{
    static const int profile_x[] = {0, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 33};
    static const float wide_profile[] = {0.5f, 0.5f, 1.0f, 4.0f, 10.0f, 13.0f, 12.0f, 9.0f, 6.0f, 3.5f, 1.0f, 0.5f};
    static const float tucked_profile[] = {0.5f, 0.5f, 1.0f, 4.0f, 8.0f, 9.0f, 8.0f, 6.0f, 4.5f, 3.0f, 1.0f, 0.5f};
    const float *profile_half_width = wings_wide ? wide_profile : tucked_profile;
    const int profile_count = sizeof(profile_x) / sizeof(profile_x[0]);

    for (int y = 0; y < 28; y++)
    {
        for (int x = 0; x < 34; x++)
        {
            float half_width = 0.0f;
            for (int p = 0; p < profile_count - 1; p++)
            {
                if (x >= profile_x[p] && x <= profile_x[p + 1])
                {
                    float t = (float)(x - profile_x[p]) / (profile_x[p + 1] - profile_x[p]);
                    half_width = profile_half_width[p] +
                                 t * (profile_half_width[p + 1] - profile_half_width[p]);
                    break;
                }
            }

            float distance_from_center = fabsf(y - 13.5f);
            uint8_t color_index = 0;
            if (distance_from_center <= half_width)
            {
                if (distance_from_center >= half_width - 1.0f)
                    color_index = 1;
                else if (x >= 19 && x <= 29 && distance_from_center <= 3.0f)
                    color_index = (x == 25 || x == 26) ? 5 : 4;
                else if (distance_from_center > 8.0f)
                    color_index = 2;
                else
                    color_index = 3;
            }
            pixels[y * 34 + x] = color_index;
        }
    }
}

static const FishSprite fish_sprites[] = {
    {26, 14, "Tetra", tetra_pixels, NULL, {0x00000000, 0x000000, 0x193D57, 0xFF2222, 0x66BBFF, 0xFFFFFF}},
    {32, 22, "Goldfish", goldfish_pixels, NULL, {0x00000000, 0x000000, 0xC94000, 0xFF7700, 0xFFCC00, 0xFFFFFF}},
    {28, 12, "Danio", danio_pixels, NULL, {0x00000000, 0x000000, 0x1A3311, 0x448822, 0xDDFF44, 0xFFFFFF}},
    {20, 10, "Neon", neon_pixels, NULL, {0x00000000, 0x000000, 0x0B2233, 0x00FFFF, 0xFF0066, 0xFFFFFF}},
    {26, 16, "Platy", platy_pixels, NULL, {0x00000000, 0x000000, 0x660022, 0xFF2277, 0xFF88BB, 0xFFFFFF}},
    {28, 14, "Mollie", mollie_pixels, NULL, {0x00000000, 0x000000, 0x220044, 0x7722CC, 0xBB66FF, 0xFFFFFF}},
    {18, 15, "Jelly", jelly_f1, jelly_f2, {0x00000000, 0x000000, 0x142B55, 0x2266CC, 0x66CCFF, 0x00FFFF}},
    {34, 28, "Manta", manta_pixels_f1, manta_pixels_f2, {0x00000000, 0x000000, 0x112244, 0x3366AA, 0x77BBFF, 0xFFFFFF}}};

#define FISH_SPRITE_COUNT \
    (sizeof(fish_sprites) / sizeof(fish_sprites[0]))

// Global arrays to hold the generated LVGL images
// Global arrays to hold the generated LVGL images
lv_img_dsc_t sprites_right_f1[SPECIES_COUNT];
lv_img_dsc_t sprites_left_f1[SPECIES_COUNT];
lv_img_dsc_t sprites_right_f2[SPECIES_COUNT];
lv_img_dsc_t sprites_left_f2[SPECIES_COUNT];

// Generate true-color LVGL images from the indexed arrays
void init_fish_sprites(void)
{
    create_manta_top_view(manta_pixels_f1, true);
    create_manta_top_view(manta_pixels_f2, false);

    for (int s = 0; s < SPECIES_COUNT; s++)
    {
        const FishSprite *fs = &fish_sprites[s];
        int w = fs->width;
        int h = fs->height;

        for (int frame = 0; frame < 2; frame++)
        {
            // If the species has no second frame, fallback to frame 1
            const uint8_t *src_pixels = (frame == 0) ? fs->pixels_f1 : fs->pixels_f2;
            if (src_pixels == NULL)
                src_pixels = fs->pixels_f1;

            lv_color_t *px_r = malloc(w * h * sizeof(lv_color_t));
            lv_color_t *px_l = malloc(w * h * sizeof(lv_color_t));

            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    uint8_t idx = src_pixels[y * w + x];
                    lv_color_t c = (idx == 0) ? LV_COLOR_CHROMA_KEY : lv_color_hex(fs->palette[idx]);
                    px_r[y * w + x] = c;
                    px_l[y * w + (w - 1 - x)] = c; // Mirror the X coordinate for the left-facing sprite
                }
            }

            lv_img_dsc_t *dsc_r = (frame == 0) ? &sprites_right_f1[s] : &sprites_right_f2[s];
            lv_img_dsc_t *dsc_l = (frame == 0) ? &sprites_left_f1[s] : &sprites_left_f2[s];

            dsc_r->header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
            dsc_r->header.always_zero = 0;
            dsc_r->header.reserved = 0;
            dsc_r->header.w = w;
            dsc_r->header.h = h;
            dsc_r->data_size = w * h * sizeof(lv_color_t);
            dsc_r->data = (const uint8_t *)px_r;

            dsc_l->header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
            dsc_l->header.always_zero = 0;
            dsc_l->header.reserved = 0;
            dsc_l->header.w = w;
            dsc_l->header.h = h;
            dsc_l->data_size = w * h * sizeof(lv_color_t);
            dsc_l->data = (const uint8_t *)px_l;
        }
    }
}

// Configure the behaviors for each fish type
const SpeciesConfig profiles[SPECIES_COUNT] = {
    {0x00D2FF, 26, 0.5, 4.0, 2.0, 10.0, 70.0, 0.025, 0.12, 0.4},  // Tetra
    {0xFF8800, 32, 0.7, 1.8, 0.5, 25.0, 55.0, 0.015, 0.04, 0.3},  // Goldfish
    {0xADFF2F, 28, 0.35, 3.2, 1.2, 15.0, 65.0, 0.02, 0.08, 0.3},  // Danio
    {0x00FFFF, 20, 0.4, 6.0, 1.5, 20.0, 60.0, 0.025, 0.14, 0.5},  // Neon
    {0xFF69B4, 26, 0.8, 2.0, 1.0, 35.0, 55.0, 0.015, 0.05, 0.25}, // Platy
    {0x8A2BE2, 28, 0.6, 2.5, 1.1, 28.0, 60.0, 0.02, 0.07, 0.28},  // Mollie
    {0xFF66FF, 18, 1.0, 2.0, 0.2, 25.0, 45.0, 0.01, 0.03, 0.3},   // Jellyfish (High burst contrast)
    {0x4466FF, 34, 0.4, 0.7, 0.25, 45.0, 90.0, 0.01, 0.05, 0.5},  // Manta (Slow, wide wingspan)
};

#define PI_F 3.14159265f

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rand() % 10000) / 10000.0f);
}

static float wrap_pi(float a)
{
    while (a > PI_F)
        a -= 2.0f * PI_F;
    while (a < -PI_F)
        a += 2.0f * PI_F;
    return a;
}

typedef struct
{
    float x, y;
    float vx, vy;
    FishSpecies species;
    int school_id;
    float heading;
    float slot_phase;
    float slot_rate;
    float slot_skew;
    float slot_r;
    float pace;
    float burst_phase;
    float burst_rate;
    float wander_turn;
    int side;
    int flip_cd;
    int last_frame;
    int16_t render_angle;
} Fish;

typedef enum
{
    TOUCH_BEHAVIOR_NONE,
    TOUCH_BEHAVIOR_FLEE,
    TOUCH_BEHAVIOR_ORBIT
} TouchBehavior;

Fish school[NUM_FISH];
static lv_obj_t *fish_canvas;
static lv_color_t *fish_canvas_buffer;
static TouchBehavior touch_behavior;
static bool touch_was_pressed;
static float touch_x;
static float touch_y;

typedef struct
{
    lv_area_t area;
    bool valid;
} FishRenderBounds;

static FishRenderBounds previous_fish_bounds[NUM_FISH];
static bool fish_dirty_tiles[FISH_DIRTY_TILE_COUNT][FISH_DIRTY_TILE_COUNT];

typedef enum
{
    SCHOOL_SWIMMING,
    SCHOOL_EXITING
} SchoolPhase;

typedef struct
{
    bool active;
    SchoolPhase phase;
    float x, y;
    float vx, vy;
    float exit_vx, exit_vy;

    // NEW: Organic wandering navigation
    float target_x, target_y;
    int timer;
    int visit_timer;
    bool avoid_schools;
    int avoid_timer;
} SchoolGroup;

SchoolGroup schools[SPECIES_COUNT];
static int next_school_to_activate = MAX_ACTIVE_SCHOOLS;

static int school_of_fish(int fish_index)
{
    int s = 0;
    while (s + 1 < SPECIES_COUNT && fish_index >= SCHOOL_FIRST[s + 1])
        s++;
    return s;
}

// Evaluates multiple random points in the tank and returns the one
// that is furthest away from all other currently active schools.
static void pick_empty_space_target(int my_school_index, float *best_x, float *best_y)
{
    float max_min_dist = -1.0f;
    float bx = 120.0f, by = 120.0f;

    // Sample 8 random coordinates in the tank
    for (int i = 0; i < 8; i++)
    {
        float tx = 120.0f + frand(-75.0f, 75.0f);
        float ty = 120.0f + frand(-75.0f, 75.0f);

        float min_dist = 9999.0f;
        for (int j = 0; j < SPECIES_COUNT; j++)
        {
            if (j == my_school_index || !schools[j].active)
                continue;

            // Measure distance to other schools
            float dx = tx - schools[j].x;
            float dy = ty - schools[j].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < min_dist)
                min_dist = d;
        }

        // If this spot is more isolated than the previous best, select it
        if (min_dist > max_min_dist)
        {
            max_min_dist = min_dist;
            bx = tx;
            by = ty;
        }
    }

    *best_x = bx;
    *best_y = by;
}

static void activate_school(int school_index)
{
    SchoolGroup *group = &schools[school_index];
    group->active = true;
    group->phase = SCHOOL_SWIMMING;
    group->avoid_schools = true;
    group->avoid_timer = (int)frand(180.0f, 420.0f);

    if (school_index == SPECIES_JELLYFISH)
    {
        group->x = frand(55.0f, 185.0f);
        group->y = 260.0f;
        group->vx = frand(-0.12f, 0.12f);
        group->vy = -0.55f;
        group->target_x = frand(0.0f, 2.0f * PI_F);
    }
    else
    {
        // Spawn completely off-screen in a random direction
        float start_angle = frand(0.0f, 2.0f * PI_F);
        group->x = 120.0f + cosf(start_angle) * 180.0f;
        group->y = 120.0f + sinf(start_angle) * 180.0f;

        // Give them an initial push towards the tank
        float entry_speed = school_index == SPECIES_MANTA ? 0.35f : 1.0f;
        group->vx = -cosf(start_angle) * entry_speed;
        group->vy = -sinf(start_angle) * entry_speed;

        // NEW: Intelligently pick the first hangout spot in an empty area
        pick_empty_space_target(school_index, &group->target_x, &group->target_y);
    }
    group->timer = (int)frand(200.0f, 400.0f);
    group->visit_timer = (int)frand(540.0f, 780.0f);

    int count = SCHOOL_SIZE[school_index];
    for (int i = 0; i < count; i++)
    {
        Fish *fish = &school[SCHOOL_FIRST[school_index] + i];
        float spread = school_index == SPECIES_JELLYFISH ? 34.0f : 14.0f + 3.0f * count;
        fish->x = group->x + frand(-spread, spread);
        fish->y = group->y + frand(-spread, spread);

        fish->vx = group->vx;
        fish->vy = group->vy;

        fish->heading = atan2f(fish->vy, fish->vx);
        fish->slot_phase = frand(0.0f, 2.0f * PI_F);
        fish->slot_rate = frand(0.010f, 0.030f) * ((rand() & 1) ? 1.0f : -1.0f);
        fish->slot_skew = frand(0.0f, 2.0f * PI_F);
        fish->slot_r = frand(8.0f, 16.0f + 2.5f * count);
        fish->pace = frand(0.88f, 1.15f);
        fish->burst_phase = frand(0.0f, 2.0f * PI_F);
        fish->burst_rate = frand(0.07f, 0.18f);
        fish->wander_turn = 0.0f;
        fish->side = (fish->vx >= 0) ? 1 : -1;
        fish->flip_cd = 0;
        fish->last_frame = 0;
    }
}

static void activate_next_school(void)
{
    for (int offset = 0; offset < SPECIES_COUNT; offset++)
    {
        int next = (next_school_to_activate + offset) % SPECIES_COUNT;
        if ((next == SPECIES_JELLYFISH && schools[SPECIES_MANTA].active) ||
            (next == SPECIES_MANTA && schools[SPECIES_JELLYFISH].active))
            continue;
        if (!schools[next].active)
        {
            activate_school(next);
            next_school_to_activate = (next + 1) % SPECIES_COUNT;
            return;
        }
    }
}

static void get_school_avoidance(int school_index, float *avoid_x, float *avoid_y)
{
    SchoolGroup *group = &schools[school_index];
    *avoid_x = 0.0f;
    *avoid_y = 0.0f;
    if (!group->avoid_schools)
        return;

    for (int other_index = 0; other_index < SPECIES_COUNT; other_index++)
    {
        if (other_index == school_index || !schools[other_index].active)
            continue;

        float dx = group->x - schools[other_index].x;
        float dy = group->y - schools[other_index].y;
        float distance_sq = dx * dx + dy * dy;
        const float avoid_distance = 100.0f;
        if (distance_sq >= avoid_distance * avoid_distance || distance_sq < 0.01f)
            continue;

        float distance = sqrtf(distance_sq);
        float strength = 1.0f - distance / avoid_distance;
        *avoid_x += (dx / distance) * strength;
        *avoid_y += (dy / distance) * strength;
    }
}

static void apply_touch_behavior(const Fish *fish, float fish_x, float fish_y, float *vx, float *vy)
{
    if (!touch_was_pressed)
        return;

    float dx = fish_x - touch_x;
    float dy = fish_y - touch_y;
    float distance_sq = dx * dx + dy * dy;

    // Prevent division by zero if a fish is dead center on the touch
    if (distance_sq < 0.01f)
    {
        *vx += frand(-1.0f, 1.0f);
        return;
    }

    float distance = sqrtf(distance_sq);
    float unit_x = dx / distance;
    float unit_y = dy / distance;

    if (touch_behavior == TOUCH_BEHAVIOR_FLEE)
    {
        // Push away strongly if within 130 pixels of the finger
        if (distance < 130.0f)
        {
            float panic_force = (130.0f - distance) * 0.08f;
            *vx += unit_x * panic_force;
            *vy += unit_y * panic_force;
        }
    }
    else if (touch_behavior == TOUCH_BEHAVIOR_ORBIT)
    {
        // Swarm and circle around the touch point at varying radii
        float target_radius = 45.0f + sinf(fish->slot_phase) * 15.0f;
        float radial_speed = (target_radius - distance) * 0.06f;

        // Cap the inward/outward pull so they don't snap violently
        if (radial_speed > 1.5f)
            radial_speed = 1.5f;
        if (radial_speed < -1.5f)
            radial_speed = -1.5f;

        // Tangential speed forces them to circle
        float tangent_speed = 1.2f;
        if (fish->slot_rate < 0.0f)
            tangent_speed = -tangent_speed; // Some swim clockwise, some counter-clockwise

        // Blend normal flocking desire (20%) with strict orbit behavior (80%)
        *vx = *vx * 0.2f + (unit_x * radial_speed - unit_y * tangent_speed) * 0.8f;
        *vy = *vy * 0.2f + (unit_y * radial_speed + unit_x * tangent_speed) * 0.8f;
    }
}

static float get_fish_sprite_scale(FishSpecies species)
{
    if (species == SPECIES_JELLYFISH)
        return 1.7f;
    if (species == SPECIES_MANTA)
        return 1.6f;
    return 1.0f;
}

static bool get_fish_sprite_bounds(const lv_img_dsc_t *sprite, float x, float y, float scale, int16_t angle, lv_area_t *area)
{
    const float width = sprite->header.w * scale;
    const float height = sprite->header.h * scale;
    const float radians = angle * (PI_F / 1800.0f);
    const float cos_angle = cosf(radians);
    const float sin_angle = sinf(radians);
    const float center_x = x + width * 0.5f;
    const float center_y = y + height * 0.5f;
    const float half_width = fabsf(cos_angle) * width * 0.5f + fabsf(sin_angle) * height * 0.5f;
    const float half_height = fabsf(sin_angle) * width * 0.5f + fabsf(cos_angle) * height * 0.5f;
    int x1 = (int)floorf(center_x - half_width);
    int x2 = (int)ceilf(center_x + half_width);
    int y1 = (int)floorf(center_y - half_height);
    int y2 = (int)ceilf(center_y + half_height);

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > 239)
        x2 = 239;
    if (y2 > 239)
        y2 = 239;
    if (x1 > x2 || y1 > y2)
        return false;

    area->x1 = x1;
    area->y1 = y1;
    area->x2 = x2;
    area->y2 = y2;
    return true;
}

static void mark_fish_dirty(const lv_area_t *area)
{
    int first_col = area->x1 / FISH_DIRTY_TILE_SIZE;
    int last_col = area->x2 / FISH_DIRTY_TILE_SIZE;
    int first_row = area->y1 / FISH_DIRTY_TILE_SIZE;
    int last_row = area->y2 / FISH_DIRTY_TILE_SIZE;

    for (int row = first_row; row <= last_row; row++)
    {
        for (int col = first_col; col <= last_col; col++)
        {
            fish_dirty_tiles[row][col] = true;
        }
    }
}

static void draw_fish_sprite(const lv_img_dsc_t *sprite, float x, float y, float scale, int16_t angle)
{
    const int width = sprite->header.w;
    const int height = sprite->header.h;
    const float radians = angle * (PI_F / 1800.0f);
    const float cos_angle = cosf(radians);
    const float sin_angle = sinf(radians);
    const float scaled_width = width * scale;
    const float scaled_height = height * scale;
    const float center_x = x + scaled_width * 0.5f;
    const float center_y = y + scaled_height * 0.5f;
    lv_area_t area;
    if (!get_fish_sprite_bounds(sprite, x, y, scale, angle, &area))
        return;

    const int x1 = area.x1;
    const int x2 = area.x2 + 1;
    const int y1 = area.y1;
    const int y2 = area.y2 + 1;
    const lv_color_t *source = (const lv_color_t *)sprite->data;
    const uint16_t chroma_key = LV_COLOR_CHROMA_KEY.full;

    // Pre-calculate how much the source X/Y changes for every 1 pixel moved in destination X
    float step_x = cos_angle / scale;
    float step_y = -sin_angle / scale;

    for (int dest_y = y1; dest_y < y2; dest_y++)
    {
        float dy = dest_y + 0.5f - center_y;
        float dx_start = x1 + 0.5f - center_x;

        // Calculate the exact source coordinate for the very first pixel in this row
        float exact_src_x = (cos_angle * dx_start + sin_angle * dy) / scale + width * 0.5f;
        float exact_src_y = (-sin_angle * dx_start + cos_angle * dy) / scale + height * 0.5f;

        for (int dest_x = x1; dest_x < x2; dest_x++)
        {
            int src_x = (int)exact_src_x;
            int src_y = (int)exact_src_y;

            if (src_x >= 0 && src_x < width && src_y >= 0 && src_y < height)
            {
                lv_color_t color = source[src_y * width + src_x];
                if (color.full != chroma_key)
                {
                    fish_canvas_buffer[dest_y * 240 + dest_x] = color;
                }
            }

            // Step to the next pixel using simple addition (Zero multiplication!)
            exact_src_x += step_x;
            exact_src_y += step_y;
        }
    }
}

static void render_fish_canvas(void)
{
    const lv_color_t transparent = LV_COLOR_CHROMA_KEY;
    for (int i = 0; i < NUM_FISH; i++)
    {
        if (previous_fish_bounds[i].valid)
        {
            lv_area_t *area = &previous_fish_bounds[i].area;
            for (int y = area->y1; y <= area->y2; y++)
            {
                for (int x = area->x1; x <= area->x2; x++)
                {
                    fish_canvas_buffer[y * 240 + x] = transparent;
                }
            }
            mark_fish_dirty(area);
            previous_fish_bounds[i].valid = false;
        }
    }

    for (int i = 0; i < NUM_FISH; i++)
    {
        Fish *fish = &school[i];
        if (!schools[fish->school_id].active)
            continue;

        const lv_img_dsc_t *sprite;
        if (fish->side == 1)
        {
            sprite = (fish->last_frame == 0) ? &sprites_right_f1[fish->species] : &sprites_right_f2[fish->species];
        }
        else
        {
            sprite = (fish->last_frame == 0) ? &sprites_left_f1[fish->species] : &sprites_left_f2[fish->species];
        }

        float scale = get_fish_sprite_scale(fish->species);
        float draw_x = fish->x - sprite->header.w * (scale - 1.0f) * 0.5f;
        float draw_y = fish->y - sprite->header.h * (scale - 1.0f) * 0.5f;
        lv_area_t area;
        if (!get_fish_sprite_bounds(sprite, draw_x, draw_y, scale, fish->render_angle, &area))
            continue;

        draw_fish_sprite(sprite, draw_x, draw_y, scale, fish->render_angle);
        previous_fish_bounds[i].area = area;
        previous_fish_bounds[i].valid = true;
        mark_fish_dirty(&area);
    }

    for (int row = 0; row < FISH_DIRTY_TILE_COUNT; row++)
    {
        int first_col = FISH_DIRTY_TILE_COUNT;
        int last_col = -1;
        for (int col = 0; col < FISH_DIRTY_TILE_COUNT; col++)
        {
            if (fish_dirty_tiles[row][col])
            {
                if (first_col > col)
                    first_col = col;
                last_col = col;
                fish_dirty_tiles[row][col] = false;
            }
        }

        if (last_col >= first_col)
        {
            lv_area_t dirty_area = {
                .x1 = first_col * FISH_DIRTY_TILE_SIZE,
                .y1 = row * FISH_DIRTY_TILE_SIZE,
                .x2 = (last_col + 1) * FISH_DIRTY_TILE_SIZE - 1,
                .y2 = (row + 1) * FISH_DIRTY_TILE_SIZE - 1,
            };
            if (dirty_area.x2 > 239)
                dirty_area.x2 = 239;
            if (dirty_area.y2 > 239)
                dirty_area.y2 = 239;
            lv_obj_invalidate_area(fish_canvas, &dirty_area);
        }
    }
}

#define NUM_BUBBLES 36

// Bubble structure (bubbles only exist during a short burst)
typedef struct
{
    lv_obj_t *obj;
    bool active; // part of a burst that is happening now
    bool shown;
    int delay; // frames until it leaves the shell
    float x, y;
    float x0, y0;
    float radius;
    float speed;
    float phase;
} Bubble;

Bubble bubbles[NUM_BUBBLES];
static int bubble_burst_timer = 10;

static lv_disp_drv_t disp_drv;

// ESP-IDF calls this when the SPI DMA transfer is physically complete
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

// --- LVGL Display Flush Callback ---
// LVGL calls this when it's time to push rendered pixels to the screen
void disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)disp_drv->user_data;

    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

#if !LV_COLOR_16_SWAP
    // GC9A01 expects big-endian RGB565; swap here if LVGL isn't doing it already
    uint32_t px_count = (uint32_t)(offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);
    uint16_t *px = (uint16_t *)color_p;
    for (uint32_t k = 0; k < px_count; k++)
    {
        px[k] = __builtin_bswap16(px[k]);
    }
#endif

    // Pass the LVGL buffer directly to the ESP-IDF panel driver
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_p);
}

// --- LVGL Touch Read Callback ---
// LVGL calls this automatically to check for input
// --- LVGL Touch Read Callback ---
// LVGL calls this automatically to check for input
void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    if (gpio_get_level(TOUCH_INT_PIN) == 0)
    {
        uint8_t rx[5];
        if (i2c_master_read_from_device(I2C_NUM_0, TOUCH_I2C_ADDR, rx, 5, pdMS_TO_TICKS(10)) == ESP_OK)
        {
            if (rx[0] == 0x01)
            {
                // Valid touch
                data->point.x = rx[2];
                data->point.y = rx[4];
                data->state = LV_INDEV_STATE_PR;
                touch_x = fminf(239.0f, fmaxf(0.0f, data->point.x));
                touch_y = fminf(239.0f, fmaxf(0.0f, data->point.y));

                if (!touch_was_pressed)
                {
                    touch_was_pressed = true;
                    // 50% chance to either Flee or Orbit for the duration of the touch
                    touch_behavior = (rand() % 2 == 0) ? TOUCH_BEHAVIOR_FLEE : TOUCH_BEHAVIOR_ORBIT;
                }
                return;
            }
        }
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    // Immediately clear behavior when finger is lifted
    if (touch_was_pressed)
    {
        touch_was_pressed = false;
        touch_behavior = TOUCH_BEHAVIOR_NONE;
    }
    data->state = LV_INDEV_STATE_REL;
}

// --- Boids Physics Timer ---
// LVGL calls this timer every 33ms (~30FPS) to update physics
void update_boids_cb(lv_timer_t *timer)
{
    for (int s = 0; s < SPECIES_COUNT; s++)
    {
        SchoolGroup *group = &schools[s];
        if (!group->active)
        {
            continue;
        }

        if (--group->avoid_timer <= 0)
        {
            group->avoid_schools = !group->avoid_schools;
            group->avoid_timer = group->avoid_schools
                                     ? (int)frand(180.0f, 420.0f)
                                     : (int)frand(30.0f, 90.0f);
        }
        float avoid_x, avoid_y;
        get_school_avoidance(s, &avoid_x, &avoid_y);

        if (s == SPECIES_JELLYFISH)
        {
            group->vx += avoid_x * 0.01f;
            if (group->vx > 0.2f)
                group->vx = 0.2f;
            if (group->vx < -0.2f)
                group->vx = -0.2f;
            group->x += group->vx + sinf(group->y * 0.025f + group->target_x) * 0.12f;
            group->y += group->vy;
            if (group->y < -60.0f)
            {
                group->active = false;
                activate_next_school();
            }
            continue;
        }

        if (group->phase == SCHOOL_SWIMMING)
        {
            group->visit_timer--;
            group->timer--;
            if (group->visit_timer <= 0)
            {
                group->phase = SCHOOL_EXITING;
                float exit_angle = frand(0.0f, 2.0f * PI_F);
                group->target_x = 120.0f + cosf(exit_angle) * 300.0f;
                group->target_y = 120.0f + sinf(exit_angle) * 300.0f;
            }

            if (group->phase == SCHOOL_SWIMMING)
            {
                if (group->timer <= 0)
                {
                    // NEW: Pick a new hangout spot dynamically based on current empty space
                    pick_empty_space_target(s, &group->target_x, &group->target_y);
                    group->timer = (int)frand(200.0f, 500.0f);
                }

                // Smoothly steer the group center towards the target.
                float dx = group->target_x - group->x;
                float dy = group->target_y - group->y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > 0)
                {
                    float max_group_speed = profiles[s].max_speed * 0.6f;

                    // NEW: Dropped the avoidance multiplier from 2.0f to 0.2f.
                    // This allows schools to peacefully glide through each other while transiting
                    // without violently scattering, while the smart targeting keeps their destinations separate.
                    float target_vx = (dx / dist + avoid_x * 0.2f) * max_group_speed;
                    float target_vy = (dy / dist + avoid_y * 0.2f) * max_group_speed;

                    group->vx += (target_vx - group->vx) * 0.015f;
                    group->vy += (target_vy - group->vy) * 0.015f;
                }
                group->x += group->vx;
                group->y += group->vy;
            }
        }
        else // SCHOOL_EXITING
        {
            float dx = group->target_x - group->x;
            float dy = group->target_y - group->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > 0)
            {
                // Speed up slightly to leave the tank
                float target_vx = (dx / dist) * 2.5f;
                float target_vy = (dy / dist) * 2.5f;
                group->vx += (target_vx - group->vx) * 0.02f;
                group->vy += (target_vy - group->vy) * 0.02f;
            }
            group->x += group->vx;
            group->y += group->vy;

            // Check if fully offscreen
            bool fully_offscreen = true;
            for (int i = 0; i < SCHOOL_SIZE[s]; i++)
            {
                Fish *fish = &school[SCHOOL_FIRST[s] + i];
                SpeciesConfig p = profiles[fish->species];
                float ox = fish->x - 120.0f;
                float oy = fish->y - 120.0f;
                if (sqrtf(ox * ox + oy * oy) < 140.0f + p.size)
                {
                    fully_offscreen = false;
                    break;
                }
            }

            if (fully_offscreen)
            {
                group->active = false;
                activate_next_school();
            }
        }
    }

    // Update fish positions
    for (int i = 0; i < NUM_FISH; i++)
    {
        int school_index = school[i].school_id;
        if (!schools[school_index].active)
        {
            continue;
        }

        Fish *f = &school[i];
        SchoolGroup *group = &schools[school_index];
        if (f->species == SPECIES_JELLYFISH)
        {
            f->slot_phase += f->slot_rate;
            f->burst_phase += f->burst_rate;
            float target_vx = group->vx + sinf(f->slot_phase + f->slot_skew) * 0.18f;
            float target_vy = group->vy - 0.08f + cosf(f->burst_phase) * 0.07f;
            apply_touch_behavior(f, f->x + 9.0f, f->y + 7.5f, &target_vx, &target_vy);
            float jelly_response = touch_was_pressed ? 0.2f : 0.08f;
            f->vx += (target_vx - f->vx) * jelly_response;
            f->vy += (target_vy - f->vy) * jelly_response;
            f->x += f->vx;
            f->y += f->vy;
            f->heading = atan2f(f->vy, f->vx);
            f->last_frame = sinf(f->burst_phase) > 0.0f ? 0 : 1;
            f->render_angle = 0;

            if (f->flip_cd > 0)
                f->flip_cd--;
            else if (f->side > 0 && f->vx < -0.08f)
            {
                f->side = -1;
                f->flip_cd = 20;
            }
            else if (f->side < 0 && f->vx > 0.08f)
            {
                f->side = 1;
                f->flip_cd = 20;
            }
            continue;
        }

        SpeciesConfig p = profiles[f->species];
        float fish_h = p.size * p.height_ratio;

        float cx = 0, cy = 0, ax = 0, ay = 0, sx = 0, sy = 0;
        int school_neighbors = 0;

        for (int j = 0; j < NUM_FISH; j++)
        {
            if (i == j || !schools[school[j].school_id].active)
                continue;

            float dx = school[j].x - f->x;
            float dy = school[j].y - f->y;
            float dist_sq = dx * dx + dy * dy; // Use squared distance first
            float neighbor_dist_sq = p.neighbor_dist * p.neighbor_dist;

            // Only run the expensive square root IF the fish is actually nearby
            if (dist_sq < neighbor_dist_sq && dist_sq > 0.001f)
            {
                float dist = sqrtf(dist_sq);

                // Separation
                if (dist < p.separation_dist)
                {
                    float push = 1.0f - dist / p.separation_dist;
                    sx -= (dx / dist) * push;
                    sy -= (dy / dist) * push;
                }

                // Cohesion & Alignment
                if (f->species == school[j].species)
                {
                    cx += school[j].x;
                    cy += school[j].y;
                    ax += school[j].vx;
                    ay += school[j].vy;
                    school_neighbors++;
                }
            }
        }

        float fcx = f->x + p.size * 0.5f;
        float fcy = f->y + fish_h * 0.5f;

        // --- Classic Reynolds Vector Steering (Replaces Rigid Orbital Tethering) ---

        // 1. Group Cohesion (Gentle pull towards the school's general direction)
        float target_pull_x = group->x - fcx;
        float target_pull_y = group->y - fcy;
        float pull_dist = sqrtf(target_pull_x * target_pull_x + target_pull_y * target_pull_y);

        // --- 1. Flocking Forces (Desired Direction) ---
        // Strongly favor the school's global trajectory to keep them moving together
        float dvx = group->vx * 1.5f;
        float dvy = group->vy * 1.5f;

        // Extremely weak tether (stops them wandering off, but prevents clumping into a ball)
        float school_dx = group->x - fcx;
        float school_dy = group->y - fcy;
        dvx += school_dx * 0.005f;
        dvy += school_dy * 0.005f;

        // Neighbor Interactions
        if (school_neighbors > 0)
        {
            cx /= school_neighbors;
            cy /= school_neighbors;
            ax /= school_neighbors;
            ay /= school_neighbors;

            // LOW Cohesion: Stops them from clustering into a dense mob
            dvx += (cx - f->x) * p.cohesion_weight * 0.3f;
            dvy += (cy - f->y) * p.cohesion_weight * 0.3f;

            // SUPER HIGH Alignment: Forces them to swim parallel in organized lines
            dvx += ax * p.alignment_weight * 4.0f;
            dvy += ay * p.alignment_weight * 4.0f;
        }

        // Smooth Separation (Removed normalization so they gently push away rather than scattering)
        dvx += sx * p.separation_weight * 3.5f;
        dvy += sy * p.separation_weight * 3.5f;

        // Touch Interaction Override
        apply_touch_behavior(f, fcx, fcy, &dvx, &dvy);

        // --- Kinematic Smoothing (Prevents Snapping) ---
        // Calculate the exact angle they *want* to swim towards based on flocking forces
        float target_heading = atan2f(dvy, dvx);

        // Substantially reduce individual wandering so they stay locked in their parallel formations
        f->wander_turn = f->wander_turn * 0.90f + frand(-1.0f, 1.0f) * 0.1f;
        target_heading += f->wander_turn * 0.15f;

        // Find the shortest turn to the target heading
        float err = wrap_pi(target_heading - f->heading);

        // STRICTLY limit how fast they can turn their bodies per frame
        float max_turn = 0.02f + 0.015f * p.max_speed; // Smooth, sweeping turns
        if (touch_was_pressed)
            max_turn *= 3.0f; // Turn faster when scared

        if (err > max_turn)
            err = max_turn;
        if (err < -max_turn)
            err = -max_turn;

        f->heading = wrap_pi(f->heading + err);

        // --- Fluid Burst-and-Glide Speed ---
        f->burst_phase += f->burst_rate;
        float target_spd = p.max_speed * f->pace * (0.4f + 0.6f * sinf(f->burst_phase));
        if (touch_was_pressed)
            target_spd = p.max_speed;

        float current_spd = sqrtf(f->vx * f->vx + f->vy * f->vy);
        current_spd += (target_spd - current_spd) * 0.1f;
        if (current_spd > p.max_speed)
            current_spd = p.max_speed;
        if (current_spd < p.min_speed)
            current_spd = p.min_speed;

        // --- Final Velocity Application ---
        // Velocity is strictly locked to heading. Impossible to snap or move sideways.
        f->vx = cosf(f->heading) * current_spd;
        f->vy = sinf(f->heading) * current_spd;

        f->x += f->vx;
        f->y += f->vy;

        // --- Determine Sprite Facing & Visual Tilt ---
        if (f->flip_cd > 0)
        {
            f->flip_cd--;
        }
        else if (f->side > 0 && f->vx < -0.15f)
        {
            f->side = -1;
            f->flip_cd = 15;
        }
        else if (f->side < 0 && f->vx > 0.15f)
        {
            f->side = 1;
            f->flip_cd = 15;
        }

        int16_t angle_deg = (int16_t)(f->heading * 180.0f / PI_F);
        if (f->side == 1)
        {
            f->render_angle = angle_deg * 10;
        }
        else
        {
            float diff = angle_deg - 180.0f;
            while (diff < -180.0f)
                diff += 360.0f;
            while (diff > 180.0f)
                diff -= 360.0f;
            f->render_angle = (int16_t)(diff * 10.0f);
        }

        f->last_frame = (sinf(f->burst_phase) > 0.0f) ? 0 : 1;
    }

    render_fish_canvas();

    // Bubbles: every now and then a small burst rises from the bottom, like a shell opening to breathe
    if (--bubble_burst_timer <= 0)
    {
        float previous_origin_x = -1.0f;
        for (int stream = 0; stream < 2; stream++)
        {
            float origin_x = frand(60.0f, 180.0f);
            for (int attempt = 0; stream > 0 && attempt < 8 &&
                                  fabsf(origin_x - previous_origin_x) < 45.0f;
                 attempt++)
            {
                origin_x = frand(60.0f, 180.0f);
            }
            previous_origin_x = origin_x;

            int count = 6 + rand() % 5;
            float delay = 0.0f;
            for (int i = 0; i < NUM_BUBBLES && count > 0; i++)
            {
                if (bubbles[i].active)
                    continue;
                Bubble *b = &bubbles[i];
                b->active = true;
                b->shown = false;
                b->delay = (int)delay;
                delay += frand(2.0f, 7.0f);
                b->x0 = origin_x + frand(-5.0f, 5.0f);
                float dx = b->x0 - 120.0f;
                b->y0 = 120.0f + sqrtf(fmaxf(0.0f, 14400.0f - dx * dx)) + 4.0f;
                b->y = b->y0;
                b->x = b->x0;
                b->speed = frand(1.0f, 1.8f);
                b->phase = frand(0.0f, 2.0f * PI_F);
                count--;
            }
        }
        bubble_burst_timer = (int)frand(90.0f, 180.0f); // next burst in 3-6 s
    }
    for (int i = 0; i < NUM_BUBBLES; i++)
    {
        Bubble *b = &bubbles[i];
        if (!b->active)
            continue;
        if (b->delay > 0)
        {
            b->delay--;
            continue;
        }
        if (!b->shown)
        {
            lv_obj_clear_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
            b->shown = true;
        }
        // Rise (speeding up a touch) with a side-to-side wobble that widens as the bubble climbs
        b->y -= b->speed;
        if (b->speed < 2.4f)
            b->speed *= 1.003f;
        b->phase += 0.12f;
        float amp = 0.8f + 0.025f * (b->y0 - b->y);
        if (amp > 4.5f)
            amp = 4.5f;
        b->x = b->x0 + sinf(b->phase) * amp;

        if (b->y < -4)
        {
            b->active = false;
            lv_obj_add_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(b->obj, (int)(b->x - b->radius), (int)(b->y - b->radius));
    }

    // --- Animate Kelp, Branches, and Bulbs ---
    for (int i = 0; i < NUM_KELP; i++)
    {
        forest[i].phase += forest[i].sway_speed;

        // Base stalk sway
        for (int j = 1; j < KELP_SEGMENTS; j++)
        {
            float sway = sinf(forest[i].phase - (j * 0.4f)) * (j * 4.0f);
            forest[i].points[j].x = forest[i].base_x + sway;
        }
        lv_line_set_points(forest[i].obj, forest[i].points, KELP_SEGMENTS);

        // Update Bulb position to track the swaying tip
        if (forest[i].bulb_obj != NULL)
        {
            lv_obj_set_pos(forest[i].bulb_obj,
                           forest[i].points[KELP_SEGMENTS - 1].x - 2,
                           forest[i].points[KELP_SEGMENTS - 1].y - 2);
        }

        // Update Branch position and bend
        if (forest[i].branch_obj != NULL)
        {
            int idx = forest[i].branch_idx;
            float bx = forest[i].points[idx].x;
            float by = forest[i].points[idx].y;

            forest[i].branch_points[0].x = bx;
            forest[i].branch_points[0].y = by;

            // Secondary sway for the branch
            float b_sway = sinf(forest[i].phase + 1.0f) * 8.0f;

            forest[i].branch_points[1].x = bx + (12.0f * forest[i].branch_dir) + b_sway;
            forest[i].branch_points[1].y = by - 10.0f;

            forest[i].branch_points[2].x = bx + (22.0f * forest[i].branch_dir) + (b_sway * 1.5f);
            forest[i].branch_points[2].y = by - 20.0f;

            lv_line_set_points(forest[i].branch_obj, forest[i].branch_points, 3);
        }
    }

    // --- Smoothly Sway Light Rays ---
    light_ray_phase += 0.02f;
    // Pan between -120 and 0 (since width is 360, it overlaps the 240 screen perfectly)
    int ray_x = -60 + (int)(sinf(light_ray_phase) * 45.0f);
    lv_obj_set_pos(light_rays_obj, ray_x, 0);
}

void app_main(void)
{
    // 1. Hardware Init (Backlight, SPI, I2C)
    gpio_reset_pin(LCD_PIN_NUM_BL);
    gpio_set_direction(LCD_PIN_NUM_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_PIN_NUM_BL, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_NUM_SCLK,
        .mosi_io_num = LCD_PIN_NUM_MOSI,
        .miso_io_num = LCD_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 240 * sizeof(uint16_t),
    };
    spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_NUM_DC,
        .cs_gpio_num = LCD_PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ORDER,
        .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle);
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, LCD_INVERT_COLOR);
    esp_lcd_panel_mirror(panel_handle, true, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0);
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TOUCH_INT_PIN, GPIO_PULLUP_ONLY);

    // 2. Initialize LVGL
    lv_init();

    // Allocate two draw buffers in DMA-capable memory and increase chunk size to 40 lines
    int buffer_lines = 40;
    lv_color_t *buf1 = heap_caps_malloc(240 * buffer_lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(240 * buffer_lines * sizeof(lv_color_t), MALLOC_CAP_DMA);

    static lv_disp_draw_buf_t draw_buf;
    // Pass BOTH buffers to LVGL
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 240 * buffer_lines);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);

    // Register Touch in LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // Generate the pixel art sprites in RAM
    init_fish_sprites();

    // 3. Build the Aquarium UI (Now safe, because a display is registered)
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x001A3D), LV_PART_MAIN); // Deep blue water
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_color_t *background_buffer = heap_caps_malloc(
        LV_CANVAS_BUF_SIZE_TRUE_COLOR(240, 240),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(background_buffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    // 1. Clean Base Gradient (No Rays)
    for (int y = 0; y < 240; y++)
    {
        float top_light = 1.0f - (float)y / 239.0f;
        top_light *= top_light;
        for (int x = 0; x < 240; x++)
        {
            float dx = ((float)x - 120.0f) / 150.0f;
            float dy = ((float)y - 55.0f) / 190.0f;
            float glow = fmaxf(0.0f, 1.0f - dx * dx - dy * dy);
            glow *= glow;
            uint8_t red = (uint8_t)(1.0f + top_light + glow);
            uint8_t green = (uint8_t)(26.0f + 4.0f * top_light + 3.0f * glow);
            uint8_t blue = (uint8_t)(61.0f + 8.0f * top_light + 7.0f * glow);
            background_buffer[y * 240 + x] = lv_color_make(red, green, blue);
        }
    }

    lv_obj_t *background_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(background_canvas, background_buffer, 240, 240, LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(background_canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // 2. Generate the 360px Wide Alpha Mask for Swaying Rays
    uint8_t *ray_alpha = heap_caps_malloc(360 * 240, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(ray_alpha != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    for (int y = 0; y < 240; y++)
    {
        float depth_fade = 1.0f - ((float)y / 150.0f);
        if (depth_fade < 0.0f)
            depth_fade = 0.0f;

        for (int x = 0; x < 360; x++)
        {
            float rays = 0.0f;
            if (depth_fade > 0.0f)
            {
                // Ray centers spread out to match the wider 360px image
                float a1 = atan2f((float)x - 90.0f, (float)y + 30.0f);
                float a2 = atan2f((float)x - 180.0f, (float)y + 30.0f);
                float a3 = atan2f((float)x - 270.0f, (float)y + 30.0f);

                rays += fmaxf(0.0f, cosf(a1 * 3.0f + 0.5f) - 0.2f) * 1.5f;
                rays += fmaxf(0.0f, cosf(a2 * 2.5f) - 0.1f) * 1.8f;
                rays += fmaxf(0.0f, cosf(a3 * 3.0f - 0.5f) - 0.2f) * 1.5f;
                rays *= (depth_fade * depth_fade);
            }
            // Scale to 8-bit opacity (max ~60/255 for subtlety)
            ray_alpha[y * 360 + x] = (uint8_t)fminf(255.0f, rays * 35.0f);
        }
    }

    ray_dsc.header.always_zero = 0;
    ray_dsc.header.w = 360;
    ray_dsc.header.h = 240;
    ray_dsc.data_size = 360 * 240;
    ray_dsc.header.cf = LV_IMG_CF_ALPHA_8BIT; // Crucial: Tells LVGL to treat this as transparency
    ray_dsc.data = ray_alpha;

    // 3. Overlay the rays and tint them Cyan
    light_rays_obj = lv_img_create(screen);
    lv_img_set_src(light_rays_obj, &ray_dsc);
    lv_obj_set_style_img_recolor(light_rays_obj, lv_color_hex(0x66EEFF), 0); // Cyan Tint
    lv_obj_set_style_img_recolor_opa(light_rays_obj, LV_OPA_COVER, 0);       // Fully applies color to the alpha mask
    lv_obj_clear_flag(light_rays_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_clear_flag(background_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(background_canvas, LV_OBJ_FLAG_CLICKABLE);

    // Disable scrollbars on the main screen
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    // Create Kelp Styles
    static lv_style_t style_kelp_dark;
    lv_style_init(&style_kelp_dark);
    lv_style_set_line_width(&style_kelp_dark, 2);
    lv_style_set_line_color(&style_kelp_dark, lv_color_hex(0x0A3A22));
    lv_style_set_line_rounded(&style_kelp_dark, true);

    static lv_style_t style_kelp_light;
    lv_style_init(&style_kelp_light);
    lv_style_set_line_width(&style_kelp_light, 1);
    lv_style_set_line_color(&style_kelp_light, lv_color_hex(0x186A32));
    lv_style_set_line_rounded(&style_kelp_light, true);

    // --- Generate the Swaying Forest (Foreground) ---
    for (int i = 0; i < NUM_KELP; i++)
    {
        forest[i].obj = lv_line_create(screen);
        lv_obj_add_style(forest[i].obj, (i % 2 == 0) ? &style_kelp_dark : &style_kelp_light, 0);

        forest[i].base_x = 20 + (i * 33) + (rand() % 15 - 7);
        forest[i].phase = ((rand() % 100) / 10.0f);
        forest[i].sway_speed = 0.04f + ((rand() % 40) / 1000.0f);

        // Massive height variance: Some are tiny sprouts (0.3), some reach the surface (1.2)
        forest[i].height_scale = 0.3f + ((rand() % 90) / 100.0f);

        for (int j = 0; j < KELP_SEGMENTS; j++)
        {
            forest[i].points[j].x = forest[i].base_x;
            forest[i].points[j].y = 240 - (j * 20 * forest[i].height_scale);
        }
        lv_line_set_points(forest[i].obj, forest[i].points, KELP_SEGMENTS);

        // Add a colorful bulb to 40% of the plants
        if (rand() % 100 < 40)
        {
            forest[i].bulb_obj = lv_obj_create(screen);
            lv_obj_set_size(forest[i].bulb_obj, 5, 5);
            lv_obj_set_style_bg_color(forest[i].bulb_obj, lv_color_hex(0xFF22AA), 0); // Neon Magenta
            lv_obj_set_style_radius(forest[i].bulb_obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(forest[i].bulb_obj, 0, 0);
            lv_obj_clear_flag(forest[i].bulb_obj, LV_OBJ_FLAG_SCROLLABLE);
        }
        else
        {
            forest[i].bulb_obj = NULL;
        }

        // Add a branch to 50% of the TALLER plants
        if (forest[i].height_scale > 0.6f && rand() % 100 < 50)
        {
            forest[i].branch_obj = lv_line_create(screen);
            lv_obj_add_style(forest[i].branch_obj, (i % 2 == 0) ? &style_kelp_dark : &style_kelp_light, 0);
            forest[i].branch_idx = 2 + (rand() % 2);                 // Sprout from the mid-to-upper segments
            forest[i].branch_dir = (rand() % 2 == 0) ? 1.0f : -1.0f; // Left or right
        }
        else
        {
            forest[i].branch_obj = NULL;
        }
    }

    // Initialize fish simulation state; their sprites are composited into one canvas.
    for (int i = 0; i < NUM_FISH; i++)
    {
        school[i].school_id = school_of_fish(i);
        school[i].species = (FishSpecies)school[i].school_id;

        school[i].x = 0;
        school[i].y = 0;
        school[i].vx = ((rand() % 100) / 50.0) - 1.0;
        school[i].vy = ((rand() % 100) / 50.0) - 1.0;

        school[i].last_frame = 0;
    }

    // Render all fish into one transparent canvas instead of managing 42 image objects.
    fish_canvas_buffer = heap_caps_malloc(
        LV_CANVAS_BUF_SIZE_TRUE_COLOR_CHROMA_KEYED(240, 240),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(fish_canvas_buffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    fish_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(fish_canvas, fish_canvas_buffer, 240, 240, LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED);
    lv_obj_set_pos(fish_canvas, 0, 0);
    lv_obj_set_style_bg_opa(fish_canvas, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(fish_canvas, 0, LV_PART_MAIN);
    lv_obj_clear_flag(fish_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(fish_canvas, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 240 * 240; i++)
    {
        fish_canvas_buffer[i] = LV_COLOR_CHROMA_KEY;
    }

    // Start with the original fish schools and let jellyfish and manta join the rotation.
    activate_school(SPECIES_TETRA);
    activate_school(SPECIES_GOLDFISH);
    activate_school(SPECIES_DANIO);
    next_school_to_activate = SPECIES_NEON;

    // Create the bubbles
    for (int i = 0; i < NUM_BUBBLES; i++)
    {
        bubbles[i].active = false;
        bubbles[i].radius = (rand() % 4 == 0) ? 2 : 1; // Tiny: mostly 1 px radius

        bubbles[i].obj = lv_obj_create(screen);
        lv_obj_set_size(bubbles[i].obj, bubbles[i].radius * 2, bubbles[i].radius * 2);

        // Because they are so small, borders won't render well. Use a semi-solid fill instead.
        lv_obj_set_style_bg_opa(bubbles[i].obj, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bubbles[i].obj, lv_color_hex(0xDDFFFF), LV_PART_MAIN);
        lv_obj_set_style_border_width(bubbles[i].obj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bubbles[i].obj, bubbles[i].radius, LV_PART_MAIN);
        lv_obj_clear_flag(bubbles[i].obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bubbles[i].obj, LV_OBJ_FLAG_HIDDEN); // hidden until a burst uses it
    }

    // Start the physics timer (~30 FPS)
    lv_timer_create(update_boids_cb, 33, NULL);

    // 4. Main LVGL Loop
    while (1)
    {
        lv_timer_handler(); // Let LVGL process timers, inputs, and drawing
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_tick_inc(10); // Tell LVGL that 10ms have passed
    }
}