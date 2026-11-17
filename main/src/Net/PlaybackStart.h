#ifndef BUTTERBOT_FIRMWARE_PLAYBACKSTART_H
#define BUTTERBOT_FIRMWARE_PLAYBACKSTART_H

#include <cstddef>
#include <cstdint>

/**
 * When AskClient may start playing a reply that is still downloading. No ESP-IDF dependencies: host-tested in
 * test/host/ask_logic.
 *
 * The ring buffer in PSRAM holds the whole reply (up to 640 KB), so the reply can be buffered for as long as
 * the link needs. Playback waits for at least MinBytes, then until the rest of the reply, at the rate measured
 * so far, will arrive before the speaker needs it, with SafetyPercent and MarginMs of headroom. A fast link
 * starts after MinBytes; a slow one waits longer and then plays without gaps.
 */
namespace PlaybackStart {

/** 16 kHz mono s16 (PcmStream::BytesPerSecond). */
inline constexpr uint32_t BytesPerSecond = 16000 * 2;

/** One second of audio: the least buffered before a reply that is still downloading may start. */
inline constexpr size_t MinBytes = BytesPerSecond;

/** The projected rest of the download is stretched by this much, for a link that slows down mid-reply. */
inline constexpr uint32_t SafetyPercent = 125;

/** Extra headroom on top, for the ring and Audio's own scheduling. */
inline constexpr uint32_t MarginMs = 500;

/**
 * @param received   reply PCM bytes received so far
 * @param replyBytes the reply's total PCM bytes, or 0 when unknown (then MinBytes is enough)
 * @param elapsedMs  time since the reply's first byte arrived
 */
bool ready(size_t received, size_t replyBytes, uint32_t elapsedMs);

}

#endif //BUTTERBOT_FIRMWARE_PLAYBACKSTART_H
