#ifndef BUTTERBOT_FIRMWARE_ASKSTATE_H
#define BUTTERBOT_FIRMWARE_ASKSTATE_H

#include <atomic>
#include <cstdint>
#include "State.h"
#include "BBData.h"
#include "CtrlData.h"
#include "Services/ISRButtonInput.h"

class Com;

/**
 * "Ask ButterBot anything" (#22): entered from ListenState when a recorded question matched no local
 * command. By then the AFE and MultiNet are unloaded (ListenState's destructor), so the network phase has
 * the internal RAM it needs.
 *
 * Offline: says the offline line. Online: blinks the status LED, says a filler line and hands the recording
 * to the boot-time AskWorker, which uploads it and plays the reply as it streams, queued behind the filler.
 * Then the line for the outcome (AskLines), and back to Idle.
 *
 * Power button, controller Listen or ShutUp cancel at any point: abort the request, stop playback, say the
 * abort phrase. RC data cancels silently and enters RC. Either way the state is left only after the worker
 * has returned (decisions #8). Controller phases: Abort on cancel, NotUnderstand on 204, nothing else.
 */
class AskState : public State {
public:
	explicit AskState(BBStateMachine* sm);

	~AskState() override;

	void tick(float deltaTime) override;

	int64_t getDynamicTickInterval() const override{ return PollMs; }

private:
	static constexpr int64_t PollMs = 100;
	/** Longest the destructor blocks the state machine if the state is ever left while the worker runs. */
	static constexpr uint32_t ExitWaitMs = 2000;

	enum class Stage : uint8_t {
		Asking,   // the worker is uploading, waiting or playing the reply
		Speaking, // a closing line (or the abort phrase) is playing
		Leaving,  // transition requested
	};

	Stage stage = Stage::Speaking;
	uint32_t turn = 0;
	Com* com = nullptr;
	bool networkLogged = false;
	// Set from event handlers, read in tick().
	std::atomic<bool> cancelled{false};
	std::atomic<bool> rcRequested{false};

	void start();

	void onWorkerDone();

	void cancel(bool sayAbort);

	void sendPhase(ListenData::Phase phase);

	void onButtonEvent(int button, ISRButtonInput::Action action);

	void onCommand(Ctrl::Command cmd);

	void onRCData(const RCData& data);
};

#endif //BUTTERBOT_FIRMWARE_ASKSTATE_H
