#ifndef MICROPY_INCLUDED_ESP8266_ESPNEOPIXEL_H
#define MICROPY_INCLUDED_ESP8266_ESPNEOPIXEL_H

void esp_neopixel_write(uint8_t pin, uint32_t *pixels, uint32_t numBytes, uint32_t config);

#endif // MICROPY_INCLUDED_ESP8266_ESPNEOPIXEL_H
