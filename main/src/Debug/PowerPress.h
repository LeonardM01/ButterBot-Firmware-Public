#ifndef BUTTERBOT_FIRMWARE_POWERPRESS_H
#define BUTTERBOT_FIRMWARE_POWERPRESS_H

#include <cstdint>

/** How a power-button press in Idle is read, from how long it was held before release. */
enum class PowerPress : uint8_t {
	Short,   // <= 0.5 s: listen (unchanged behaviour)
	Medium,  // 1.0-2.5 s: the measurement build's debug ask turn
	Ignored, // anything else; a 3 s hold shuts the robot down elsewhere
};

inline constexpr uint64_t ShortPressMaxMs = 500;
inline constexpr uint64_t MediumPressMinMs = 1000;
inline constexpr uint64_t MediumPressMaxMs = 2500;

PowerPress classifyPowerPress(uint64_t heldMs);

#endif //BUTTERBOT_FIRMWARE_POWERPRESS_H
