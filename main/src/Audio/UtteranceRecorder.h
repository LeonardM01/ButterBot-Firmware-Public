#ifndef BUTTERBOT_FIRMWARE_UTTERANCERECORDER_H
#define BUTTERBOT_FIRMWARE_UTTERANCERECORDER_H

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * Records one spoken question from the AFE's output into a caller-owned buffer (#22, decisions #1, #2).
 *
 * Nothing is kept until the VAD reports speech. At onset the AFE's VAD pre-roll is stored first, then
 * every frame while the VAD says speech. The utterance ends at the first speech -> silence edge (the AFE
 * VAD only flips after ~1 s of silence, vad_min_noise_ms) or when the buffer is full. The capacity is the
 * cap and counts everything written, pre-roll included; it is used in whole 16-bit samples only, so
 * odd-sized input loses its trailing byte.
 *
 * Pure logic, no allocation; one task feeds it (the AFE proc task) and others read it only once the
 * feeder has stopped.
 */
class UtteranceRecorder {
public:
	enum class State : uint8_t {
		Waiting,  // no speech yet, nothing recorded
		Speaking, // recording
		Done,     // the utterance is over; see end()
	};

	enum class End : uint8_t {
		None,    // not done
		Silence, // the speaker stopped
		Cap,     // the buffer is full
	};

	/** `pcm` holds at least `capacity` bytes; the usable capacity is rounded down to whole samples. */
	UtteranceRecorder(uint8_t* pcm, size_t capacity);

	/** Forgets the last utterance; the next onFetch() starts a new one in the same buffer. */
	void reset();

	/**
	 * One AFE fetch: `speech` is its VAD state, `preRoll` the VAD cache it hands over at speech onset
	 * (empty otherwise) and `frame` its output.
	 */
	void onFetch(bool speech, std::span<const uint8_t> preRoll, std::span<const uint8_t> frame);

	State state() const{ return current; }

	End end() const{ return ending; }

	/** PCM bytes recorded, always even. */
	size_t bytes() const{ return used; }

	/** True once the VAD reported speech in this utterance. */
	bool heardSpeech() const{ return current != State::Waiting; }

private:
	uint8_t* const pcm;
	const size_t capacity;
	size_t used = 0;
	State current = State::Waiting;
	End ending = End::None;

	void append(std::span<const uint8_t> data);

	void finish(End why);
};

#endif //BUTTERBOT_FIRMWARE_UTTERANCERECORDER_H
