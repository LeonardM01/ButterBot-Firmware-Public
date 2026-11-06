#ifndef BUTTERBOT_FIRMWARE_MEASUREFORMAT_H
#define BUTTERBOT_FIRMWARE_MEASUREFORMAT_H

#include <cstddef>
#include <cstdint>
#include "Net/AskClient.h"

/**
 * Text for the measurement build's serial lines. Every line starts with "BBMEASURE" and is
 * key=value pairs, so `grep BBMEASURE` pulls the numbers out of a serial log.
 * No ESP-IDF dependencies: host-tested in test/host/measure_logic.
 */

inline constexpr size_t MeasureLineBytes = 320; // fits the longest ask line

/** Writes bytes/ms as KiB/s (1 KiB = 1024 B) with one decimal, or "-" when ms is 0. */
void formatKiBps(char* out, size_t cap, uint32_t bytes, uint32_t ms);

/** Writes the per-turn summary from AskResult. Always NUL-terminated; cut if `cap` is too small. */
void formatAskMeasure(char* out, size_t cap, const AskResult& r);

#endif //BUTTERBOT_FIRMWARE_MEASUREFORMAT_H
