#ifndef BUTTERBOT_FIRMWARE_AUDIOFRONTEND_H
#define BUTTERBOT_FIRMWARE_AUDIOFRONTEND_H

#include "Object/Object.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <atomic>
#include <vector>
#include "esp_vad.h"
#include "esp_mn_iface.h"
#include "MicInput.h"
#include "Util/PSRAMAllocator.h"
#include "Thread/Threaded.h"
#include "Event/EventBroadcaster.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_iface.h"
#include "model_path.h"
#include "Services/LEDControl.h"
#include "UtteranceRecorder.h"
#if BB_DEBUG_ASK_BUILD
#include <optional>
#include "Debug/AfeGlitchMeter.h"
#endif

class AudioFrontend : public Object {
	GENERATED_BODY(AudioFrontend, Object, void);
public:
	AudioFrontend();

	~AudioFrontend() override;

	enum class Mode : uint8_t {
		Audio, Wake, Speech, VAD, Off
	};

	void setMode(Mode mode);

	static constexpr float DefaultFuzzyThreshold = 0.2f;

	struct Phrase {
		const char* string; //Phrase string (graphemes)
		const char* phonemes; //Phrase phonemes, generated using multinet_g2p.py[esp-sr/tool/multinet_g2p.py]
		const char* fuzzyCore = nullptr;
		float fuzzyThreshold = DefaultFuzzyThreshold;
		const void* action = nullptr;
	};

	struct PhraseResult {
		bool recognized;
		int index;
		std::string transcript;
		float confidence;
	};

	/**
	 * Clears previous phrases and replaces them with specified phrases.
	 */
	void setSpeechPhrases(std::span<Phrase> phrases);

	/**
	 * Blocks until a phrase is detected or the recognition window times out.
	 * Mode must be Off before calling. Sets mode to Speech internally and restores it to Off on return.
	 */
	PhraseResult waitForPhrase();

	void setOutputBuffer(RingbufHandle_t rb);

	/**
	 * Adjust voice activity detection parameters.
	 * Can only be called when AudioFrontend's Mode is set to Off
	 * @param vad_mode The value can be: VAD_MODE_0 to VAD_MODE_4, ranging from least to most aggressive detection rate
	 * @param min_speech_ms The minimum duration of speech in ms. It should be bigger than 32 ms, default: 128 ms
	 * @param min_noise_ms The minimum duration of noise or silence in ms. It should be bigger than 64 ms, default: 1000 ms
	 * @param vad_delay_ms The delay of the first speech frame in ms, default: 128 ms
	 */
	void setVADParams(vad_mode_t vad_mode, int min_speech_ms, int min_noise_ms, int vad_delay_ms);


	/**
	 * size_t samples - number of samples sent to output buffer; 0 - outputBuffer is full
	 */
	DECLARE_EVENT(AudioEvent, AudioFrontend, size_t)
	AudioEvent onAudio = AudioEvent(this);

	/**
	 * bool recognized - true if a registered phrase (or abort phrase) was detected, false if timeout - also invalidates all values except transcript
	 * int index - index of detected phrase, from previously set phrases with setSpeechPhrases(), -1 for abort phrases
	 * std::string transcript - phonemic transcript of detected phrase
	 * float confidence - confidence of detection
	 */
	DECLARE_EVENT(PhraseEvent, AudioFrontend, bool, int, std::string, float)
	PhraseEvent onPhrase = PhraseEvent(this);

	/**
	 * bool active - true when voice activity is ongoing, false when no voice activity.
	 * Fires only on changes; the first fetch after entering Mode::VAD always fires
	 * to publish the initial state.
	 */
	DECLARE_EVENT(VADEvent, AudioFrontend, bool)
	VADEvent onVAD = VADEvent(this);

	/**
	 * Loads AFE + MultiNet (~3.5 MB PSRAM, ~17 KB internal steady). While it loads, plain malloc() goes to PSRAM
	 * first, so the load's many small allocations don't drain internal RAM (docs/voice-turn-memory.md).
	 * Only while the mode is Off. False when the models could not be loaded; nothing is left half-loaded then.
	 * An out-of-memory reboots instead (CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS, M11).
	 */
	bool loadModel();
	void unloadModel();

	/**
	 * Records the next Speech session into `recorder` (the voice turn, #22); nullptr stops recording.
	 * Only while the mode is Off; setMode(Off) detaches it again.
	 *
	 * With a recorder attached, one Speech session reports exactly one onPhrase:
	 * - a MultiNet command or abort phrase, as without recording;
	 * - otherwise, once the utterance has ended (silence or the recorder's cap), the fuzzy decision on what
	 *   MultiNet heard so far, without waiting for its 4 s window (decisions #1);
	 * - or MultiNet's timeout before any speech: recognized = false with nothing recorded.
	 * If MultiNet times out while the user is still speaking, recording goes on until the utterance ends.
	 */
	void setCapture(UtteranceRecorder* recorder);

#if BB_DEBUG_ASK_BUILD
	/**
	 * Measurement build only (#20): starts counting audio-input glitches (late mic reads, AFE backlog,
	 * failed reads and fetches) from zero. Needs the model loaded and the mode Off.
	 */
	void resetGlitchMeter();

	/** Counters since resetGlitchMeter(); all zero if it was never called. */
	AfeGlitchMeter::Stats glitchStats() const;
#endif

private:
#if BB_DEBUG_ASK_BUILD
	// Created by resetGlitchMeter() while the feed and fetch tasks wait at their gates.
	std::optional<AfeGlitchMeter> glitchMeter;
#endif

	void feedTaskFunc();

	std::unique_ptr<Threaded> feedTask;

	void procTaskFunc();

	std::unique_ptr<Threaded> procTask;

	/**
	 * Event group that coordinates whether the feed/proc tasks should be doing work.
	 * When RunBit is clear, each task blocks at the start of its loop on its respective gate
	 * until RunBit is set again. AtGate bits let setMode/the constructor wait until the tasks
	 * have actually reached the blocked state before mutating shared state.
	 */
	EventGroupHandle_t taskGate = nullptr;
	static constexpr EventBits_t RunBit = BIT0;
	static constexpr EventBits_t FeedAtGate = BIT1;
	static constexpr EventBits_t ProcAtGate = BIT2;

	void waitAtGate(EventBits_t atGateBit);

	void processAudio(afe_fetch_result_t* res);

	void processSpeech(afe_fetch_result_t* res);

	void processVAD(afe_fetch_result_t* res);

	/** Feeds the attached recorder. True when MultiNet is done and only the recording is still running. */
	bool recordSpeech(afe_fetch_result_t* res, UtteranceRecorder* recorder);

	/** Reports fuzzyMatch(lastRawTranscript)'s result as MultiNet's timeout does: no match, abort or a phrase. */
	void reportFuzzy(int fuzzyIndex);

	// Voice turn recording (see setCapture). Set by the state machine task while the AFE tasks are parked
	// at their gate, then used by the proc task only.
	std::atomic<UtteranceRecorder*> capture{nullptr};
	std::atomic<bool> speechReported{false};       // this Speech session's result is out
	std::atomic<bool> awaitingUtteranceEnd{false}; // MultiNet found nothing, the speaker is still talking

	// Last vad_state observed in Mode::VAD that was broadcast on onVAD.
	// -1 is a sentinel meaning "nothing fired yet"; used to force the first
	// fetch after entering VAD mode to publish the initial state.
	std::atomic<int8_t> lastVADState{ -1 };

	std::atomic<Mode> currentMode = Mode::Off;

	RingbufHandle_t outputBuffer = nullptr;

	srmodel_list_t* models = nullptr;

	afe_config_t* afe_config = nullptr;
	const esp_afe_sr_iface_t* afe_handle = nullptr;
	esp_afe_sr_data_t* afe_data = nullptr;

	esp_mn_iface_t* multinet_handle = nullptr;
	model_iface_data_t* multinet_data = nullptr;
	bool mnCommandsAllocated = false;

	/** loadModel()'s steps. False at the first one that fails, leaving what was made for releaseModels(). */
	bool createModels();

	/** Frees whatever createModels() made, in reverse order; safe on a partial load. */
	void releaseModels();

	//MultiNet speech recognition buffer size
	static constexpr int32_t MultinetDuration = 4000; //[ms]

	static constexpr float MultinetDetThreshold = 0.35f;

	/*
	 * FeedBuffer size is inferred from chunksize in runtime, since it can depend on available AFE features and config.
	 * This would change if the underlying AudioFrontend's configuration is modified.
	 */
	static constexpr size_t FeedBufferSize = 1024;
	PSRAMVector<int16_t> feedBuffer; //Buffer used by feedTask; sized to FeedBufferSize in ctor, backed by PSRAM

	SemaphoreHandle_t phraseSem = nullptr;
	PhraseResult phraseWaitResult{};
	std::atomic<bool> phraseWaiting{ false };

	std::string lastRawTranscript;

	std::vector<Phrase> speechPhrases;

	int fuzzyMatch(const std::string& transcript);

	void reportPhrase(bool recognized, int index, const std::string& transcript, float confidence);

	static constexpr int AbortAction{};

	static constexpr Phrase AbortPhrases[] = {
		{ "cancel", "KaNScL", "KaNScL", DefaultFuzzyThreshold, &AbortAction },
		{ "nevermind", "NfVkMiND", "NfVkMiND", DefaultFuzzyThreshold, &AbortAction }
	};
	static constexpr uint8_t AbortPhrasesNum = 2;
};


#endif //BUTTERBOT_FIRMWARE_AUDIOFRONTEND_H
