#include "Display.h"
#include "Board.h"
#include <driver/spi_master.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace Display {

namespace {
// SPI 3 fils (9 bits) pour l'initialisation du ST7701, CS sur l'extenseur (EXIO3)
constexpr int PIN_SPI_SCK = 2;
constexpr int PIN_SPI_MOSI = 1;

// Bus RGB
constexpr int PIN_HSYNC = 38, PIN_VSYNC = 39, PIN_DE = 40, PIN_PCLK = 41;
constexpr int PIN_DATA[16] = {5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17};

struct InitCmd {
  uint8_t cmd;
  uint8_t len;
  uint8_t data[16];
  uint16_t delayMs;
};

// Séquence d'initialisation Waveshare (Display_ST7701.cpp), reprise telle quelle
const InitCmd INIT[] = {
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}, 0},
  {0xC0, 2, {0x3B, 0x00}, 0},
  {0xC1, 2, {0x0B, 0x02}, 0},
  {0xC2, 2, {0x07, 0x02}, 0},
  {0xCC, 1, {0x10}, 0},
  {0xCD, 1, {0x08}, 0},
  {0xB0, 16, {0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 0},
  {0xB1, 16, {0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}, 0},
  {0xB0, 1, {0x6D}, 0},
  {0xB1, 1, {0x37}, 0},
  {0xB2, 1, {0x81}, 0},
  {0xB3, 1, {0x80}, 0},
  {0xB5, 1, {0x43}, 0},
  {0xB7, 1, {0x85}, 0},
  {0xB8, 1, {0x20}, 0},
  {0xC1, 1, {0x78}, 0},
  {0xC2, 1, {0x78}, 0},
  {0xD0, 1, {0x88}, 0},
  {0xE0, 3, {0x00, 0x00, 0x02}, 0},
  {0xE1, 11, {0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 0},
  {0xE2, 13, {0}, 0},
  {0xE3, 4, {0x00, 0x00, 0x11, 0x00}, 0},
  {0xE4, 2, {0x22, 0x00}, 0},
  {0xE5, 16, {0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
  {0xE6, 4, {0x00, 0x00, 0x11, 0x00}, 0},
  {0xE7, 2, {0x22, 0x00}, 0},
  {0xE8, 16, {0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
  {0xEB, 7, {0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 0},
  {0xED, 16, {0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 0},
  {0xEF, 6, {0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
  {0xEF, 1, {0x08}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}, 0},
  {0x36, 1, {0x00}, 0},
  {0x3A, 1, {0x66}, 0},
  {0x11, 0, {0}, 480},   // sortie de veille
  {0x20, 0, {0}, 120},   // pas d'inversion
  {0x29, 0, {0}, 0},     // affichage allumé
};

spi_device_handle_t spi = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t vsyncSem = nullptr;
uint16_t* fbs[2] = {nullptr, nullptr};
int back = 1;

void spiWrite(bool isData, uint8_t value) {
  spi_transaction_t t = {};
  t.cmd = isData ? 1 : 0;
  t.addr = value;
  t.length = 0;
  spi_device_transmit(spi, &t);
}

bool sendInitSequence() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_SPI_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_SPI_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 64;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.command_bits = 1;    // bit D/C
  dev.address_bits = 8;    // octet transmis
  dev.mode = 0;
  dev.clock_speed_hz = 40 * 1000 * 1000;
  dev.spics_io_num = -1;
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return false;

  Board::setExio(Board::EXIO_LCD_RST, false);
  delay(10);
  Board::setExio(Board::EXIO_LCD_RST, true);
  delay(50);

  Board::setExio(Board::EXIO_LCD_CS, false);
  delay(10);
  for (const InitCmd& c : INIT) {
    spiWrite(false, c.cmd);
    for (uint8_t i = 0; i < c.len; i++) spiWrite(true, c.data[i]);
    if (c.delayMs) delay(c.delayMs);
  }
  Board::setExio(Board::EXIO_LCD_CS, true);
  return true;
}

// Appelée quand le pilote a fini de recopier une trame entière : il passe alors au tampon demandé
// par draw_bitmap, et l'ancien peut être redessiné sans risque. (Le retour de trame VSYNC arrive
// trop tôt : à 30 images/s on redessinait parfois le tampon encore affiché → image qui tremble.)
bool IRAM_ATTR onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSem, &woken);
  return woken == pdTRUE;
}
}  // namespace

bool begin() {
  if (!sendInitSequence()) return false;

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = 16 * 1000 * 1000;
  cfg.timings.h_res = WIDTH;
  cfg.timings.v_res = HEIGHT;
  cfg.timings.hsync_pulse_width = 8;
  cfg.timings.hsync_back_porch = 10;
  cfg.timings.hsync_front_porch = 50;
  cfg.timings.vsync_pulse_width = 3;
  cfg.timings.vsync_back_porch = 8;
  cfg.timings.vsync_front_porch = 8;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  cfg.bounce_buffer_size_px = 10 * WIDTH;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  cfg.dma_burst_size = 64;
#else
  cfg.psram_trans_align = 64;
#endif
  cfg.hsync_gpio_num = PIN_HSYNC;
  cfg.vsync_gpio_num = PIN_VSYNC;
  cfg.de_gpio_num = PIN_DE;
  cfg.pclk_gpio_num = PIN_PCLK;
  cfg.disp_gpio_num = -1;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = PIN_DATA[i];
  cfg.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return false;

  vsyncSem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
  cbs.on_frame_buf_complete = onFrameDone;
#else
  cbs.on_bounce_frame_finish = onFrameDone;
#endif
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);

  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  esp_lcd_rgb_panel_get_frame_buffer(panel, 2, (void**)&fbs[0], (void**)&fbs[1]);
  return fbs[0] && fbs[1];
}

uint16_t* backBuffer() { return fbs[back]; }

void sleep() {
  if (!spi) return;
  Board::setExio(Board::EXIO_LCD_CS, false);
  spiWrite(false, 0x28);   // affichage éteint
  delay(20);
  spiWrite(false, 0x10);   // veille du contrôleur
  delay(120);
  Board::setExio(Board::EXIO_LCD_CS, true);
}

void present() {
  xSemaphoreTake(vsyncSem, 0);   // oublie une fin de trame ancienne
  // Passer l'un des tampons du pilote ne copie rien : il devient la trame affichée.
  esp_lcd_panel_draw_bitmap(panel, 0, 0, WIDTH, HEIGHT, fbs[back]);
  xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(50));
  back ^= 1;
}

}  // namespace Display
