#ifndef BUTTERBOT_FIRMWARE_DEBUGASKSTATE_H
#define BUTTERBOT_FIRMWARE_DEBUGASKSTATE_H

#if BB_DEBUG_ASK_BUILD

#include "State.h"
#include "CtrlData.h"
#include "Services/ISRButtonInput.h"

/**
 * Measurement build only (BB_DEBUG_ASK 1): holds the robot out of Idle while a debug ask turn runs on
 * its worker task, so no routine or listen turn touches the AFE or Audio meanwhile. Entered from Idle
 * on a medium power-button press; back to Idle when the turn has cleaned up.
 *
 * A power-button press aborts the turn. RC data aborts it and enters RC mode.
 */
class DebugAskState : public State {
public:
	explicit DebugAskState(BBStateMachine* sm);

	~DebugAskState() override;

	void tick(float deltaTime) override;

	int64_t getDynamicTickInterval() const override{ return PollMs; }

private:
	static constexpr int64_t PollMs = 100;

	void onButtonEvent(int button, ISRButtonInput::Action action);

	void onRCData(const RCData& data);
};

#endif

#endif //BUTTERBOT_FIRMWARE_DEBUGASKSTATE_H
