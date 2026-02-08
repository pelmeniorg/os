
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"



/* ═══════════════════════════════════════════════════════════════════
   Limine Requests — магические структуры для запроса сервисов
   ═══════════════════════════════════════════════════════════════════ */

// Обязательно: базовая ревизия протокола
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

// Запрос фреймбуфера (экран)
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Маркер конца запросов
__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

/* ═══════════════════════════════════════════════════════════════════
   Вспомогательные функции
   ═══════════════════════════════════════════════════════════════════ */

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void put_pixel(struct limine_framebuffer *fb,
                      uint64_t x, uint64_t y,
                      uint32_t color) {
    volatile uint32_t *pixel = (volatile uint32_t *)(
        (uintptr_t)fb->address + y * fb->pitch + x * 4
    );
    *pixel = color;
}

static void fill_rect(struct limine_framebuffer *fb,
                      uint64_t x, uint64_t y,
                      uint64_t w, uint64_t h,
                      uint32_t color) {
    for (uint64_t py = y; py < y + h && py < fb->height; py++) {
        for (uint64_t px = x; px < x + w && px < fb->width; px++) {
            put_pixel(fb, px, py, color);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Rust интерфейс
   ═══════════════════════════════════════════════════════════════════ */

extern void rust_entry(void);

/* ═══════════════════════════════════════════════════════════════════
   Точка входа ядра
   ═══════════════════════════════════════════════════════════════════ */

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    fill_rect(fb, 0, 0, fb->width, fb->height, 0x001a1a2e);

    fill_rect(fb, 50,  50, 100, 100, 0x00ff6b6b);
    fill_rect(fb, 200, 50, 100, 100, 0x0048dbfb);
    fill_rect(fb, 350, 50, 100, 100, 0x001dd1a1);


    hcf();
}