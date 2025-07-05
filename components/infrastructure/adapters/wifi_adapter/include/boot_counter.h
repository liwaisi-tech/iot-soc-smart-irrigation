#ifndef BOOT_COUNTER_H
#define BOOT_COUNTER_H

#include "esp_err.h"
#include "esp_system.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_COUNTER_MAGIC_NUMBER 0xDEADBEEF
#define BOOT_COUNTER_RESET_THRESHOLD 5
#define BOOT_COUNTER_TIME_WINDOW_MS 30000

typedef struct {
    uint32_t magic_number;
    uint32_t boot_count;
    uint32_t last_boot_time;
    uint32_t first_boot_time;
} boot_counter_t;

esp_err_t boot_counter_init(void);

esp_err_t boot_counter_increment(void);

esp_err_t boot_counter_clear(void);

esp_err_t boot_counter_check_reset_pattern(bool *should_provision);

esp_err_t boot_counter_get_count(uint32_t *count);

esp_err_t boot_counter_set_normal_operation(void);

bool boot_counter_is_in_time_window(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_COUNTER_H */