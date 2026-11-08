#ifndef BUTTERBOT_FIRMWARE_DEBUGASKSTATE_H
#define BUTTERBOT_FIRMWARE_DEBUGASKSTATE_H

#if BB_DEBUG_ASK_BUILD

#include "State.h"
#include "CtrlData.h"
#include "Services/ISRButtonInput.h"

/**
 * Measurement build only (BB_DEBUG_ASK 1): holds the robot out of Idle while a debug ask turn runs (its steps
 * in tick(), the request on the shared AskWorker), so no routine or listen turn touches the AFE or Audio meanwhile. Entered from Idle
 * on a medium power-button press; back to Idle when the turn has cleaned up.
 *
 * A power-button press aborts the turn. RC data aborts it and enters RC mode. Either way the state is left
 * only once the worker has returned (normally within about 0.5 s; see AskClient::abortInFlight),
 * so the next state can use the AFE and Audio safely. The state machine keeps ticking meanwhile.
 */
class DebugAskState : public State {
public:
	explicit DebugAskState(BBStateMachine* sm);

	~DebugAskState() override;

	void tick(float deltaTime) override;

	int64_t getDynamicTickInterval() const override{ return PollMs; }

private:
	static constexpr int64_t PollMs = 100;
	/** How often the destructor warns while it waits out a worker that is still running. */
	static constexpr uint32_t ExitWaitMs = 2000;

	bool rcRequested = false;
	bool ledBlinking = false;

	void onButtonEvent(int button, ISRButtonInput::Action action);

	void onRCData(const RCData& data);
};

#endif

#endif //BUTTERBOT_FIRMWARE_DEBUGASKSTATE_H
