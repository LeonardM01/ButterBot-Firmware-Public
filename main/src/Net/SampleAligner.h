#ifndef BUTTERBOT_FIRMWARE_SAMPLEALIGNER_H
#define BUTTERBOT_FIRMWARE_SAMPLEALIGNER_H

#include <cstddef>
#include <cstdint>

/**
 * Keeps 16-bit sample alignment across network reads that can end on an odd byte.
 * Each push releases up to two spans of whole samples, to be written in order:
 * a completed pair (the byte held back last time + the first new byte), then the
 * even-length body. A trailing odd byte is held back for the next push.
 */
class SampleAligner {
public:
	struct Aligned {
		const uint8_t* pair; // points into the aligner, valid until the next push
		size_t pairLen;      // 0 or 2
		const uint8_t* body; // points into the pushed data
		size_t bodyLen;      // even
	};

	Aligned push(const uint8_t* data, size_t len);

	bool hasCarry() const;

private:
	uint8_t pair[2] = {}; // last released pair
	uint8_t held = 0;     // odd byte waiting for its partner
	bool carry = false;
};

#endif //BUTTERBOT_FIRMWARE_SAMPLEALIGNER_H
