#include "DebugAskState.h"

#if BB_DEBUG_ASK_BUILD

#include <Core/Application.h>
#include <Log/Log.h>
#include <Services/ControllerState.h>
#include <Util/ServiceLocator.h>
#include "BBStateMachine.h"
#include "Debug/DebugAsk.h"
#include "Enums.h"
#include "IdleState.h"
#include "RCState.h"
#include "Services/Com.h"

DEFINE_LOG(DebugAskState)

DebugAskState::DebugAskState(BBStateMachine* sm) : State(sm){
	const Application* app = Application::getApp();

	if(ISRButtonInput* buttons = app->getService<ISRButtonInput>()){
		sm->bindState(buttons->OnButtonEvent, this, &DebugAskState::onButtonEvent);
	}
	if(Com* com = app->getService<Com>()){
		sm->bindState(com->OnRCData, this, &DebugAskState::onRCData);
	}

	if(!DebugAsk::start()){
		CMF_LOG(DebugAskState, LogLevel::Warning, "debug ask turn did not start");
		return;
	}

	if(ServiceLocator::LEDControlInstance){
		ServiceLocator::LEDControlInstance->startListen();
	}
}

DebugAskState::~DebugAskState(){
	// Normally the turn has cleaned up already: tick() leaves only once it has. This is the backstop for any
	// other way out, so the next state never finds a reply still playing behind its back.
	DebugAsk::stop(ExitWaitMs);
	if(ServiceLocator::LEDControlInstance){
		ServiceLocator::LEDControlInstance->stopListen();
	}
}

void DebugAskState::tick(float deltaTime){
	// Leave only after the turn is over (the models are unloaded within its first steps, the worker has
	// returned), so whatever comes next (a listen turn from Idle, RC mode) finds the AFE and Audio free.
	if(sm->isTransitionPending() || DebugAsk::poll()) return;

	if(rcRequested){
		sm->transitionTo<RCState>();
	}else{
		sm->transitionTo<IdleState>();
	}
}

void DebugAskState::onButtonEvent(int button, ISRButtonInput::Action action){
	if(button == static_cast<int>(Button::Power) && action == ISRButtonInput::Action::Press){
		DebugAsk::abort();
	}
}

void DebugAskState::onRCData(const RCData& data){
	if(ServiceLocator::ControllerStateInstance){
		ServiceLocator::ControllerStateInstance->updateRCData(data);
	}
	// RCState aborts the turn too, but the worker has to clean up before the state changes.
	DebugAsk::abort();
	rcRequested = true;
}

#endif
