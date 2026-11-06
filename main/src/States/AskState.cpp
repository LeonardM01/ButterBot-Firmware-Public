#include "AskState.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <Core/Application.h>
#include <Log/Log.h>
#include <Phrases.h>
#include <Services/Audio/Audio.h>
#include <Services/ControllerState.h>
#include <Util/ServiceLocator.h>
#include "Audio/SpeechAudioSource.h"
#include "BBStateMachine.h"
#include "Enums.h"
#include "IdleState.h"
#include "Net/AskClient.h"
#include "Net/AskLines.h"
#include "Net/AskWorker.h"
#include "Audio/VoiceCapture.h"
#include "RCState.h"
#include "Services/Com.h"
#include "Util/VoiceTurnHeap.h"

DEFINE_LOG(AskState)

namespace {

Audio* audioService(){
	return Application::getApp()->getService<Audio>();
}

enum class Cut : uint8_t { Now, AfterCurrent };

/** Says `text` through flite: cutting whatever plays, or queued behind it (a filler is never cut, decisions #5). */
void say(const std::string& text, Cut cut){
	Audio* audio = audioService();
	if(audio == nullptr || text.empty() || !ServiceLocator::SpeechAudioGenInstance || !ServiceLocator::SpeechGenInstance){
		return;
	}
	auto source = std::make_unique<SpeechAudioSource>(SpeechGen::InputType::Text, text);
	if(cut == Cut::Now){
		audio->play(ServiceLocator::SpeechAudioGenInstance.get(), std::move(source));
	}else{
		audio->enqueue(ServiceLocator::SpeechAudioGenInstance.get(), std::move(source));
	}
}

void sayPhrase(Phrase phrase, Cut cut){
	const int16_t id = Phrases::get(phrase);
	if(id >= 0){
		say(Phrases::map(phrase, id), cut);
	}
}

void sayLine(AskLine line, Cut cut){
	if(line == AskLine::DidntUnderstand){
		sayPhrase(Phrase::ListeningTimeout, cut);
		return;
	}
	if(const char* text = askLineText(line, (uint32_t) rand())){
		say(text, cut);
	}
}

void setLed(bool on){
	if(!ServiceLocator::LEDControlInstance) return;
	if(on){
		ServiceLocator::LEDControlInstance->startListen();
	}else{
		ServiceLocator::LEDControlInstance->stopListen();
	}
}

}

void AskState::setThinkingLed(bool on){
	thinkingLed = on;
	setLed(on);
}

AskState::AskState(BBStateMachine* sm) : State(sm){
	// Armed first, so a cancel from here on is never lost.
	turn = AskClient::beginTurn();

	const Application* app = Application::getApp();
	if(ISRButtonInput* buttons = app->getService<ISRButtonInput>()){
		sm->bindState(buttons->OnButtonEvent, this, &AskState::onButtonEvent);
	}
	com = app->getService<Com>();
	if(com != nullptr){
		sm->bindState(com->OnCommand, this, &AskState::onCommand);
		sm->bindState(com->OnRCData, this, &AskState::onRCData);
	}

	start();
}

void AskState::start(){
	const WiFiService* wifi = ServiceLocator::WiFiServiceInstance;
	if(wifi == nullptr || !wifi->isOnline()){
		CMF_LOG(AskState, LogLevel::Info, "ask: offline");
		sayLine(lineFor(false, AskResult{}), Cut::Now);
		return;
	}

	VoiceCapture* capture = ServiceLocator::VoiceCaptureInstance;
	AskWorker* worker = ServiceLocator::AskWorkerInstance;
	const std::span<const uint8_t> wav = capture != nullptr ? capture->wav() : std::span<const uint8_t>{};
	if(worker == nullptr || wav.empty()){
		CMF_LOG(AskState, LogLevel::Warning, "ask: no %s", worker == nullptr ? "worker" : "recording");
		sayLine(AskLine::ThoughtsLost, Cut::Now);
		return;
	}

	setThinkingLed(true);
	say(askFillerText((uint32_t) rand()), Cut::Now);
	if(!worker->submit(turn, wav.data(), wav.size())){
		CMF_LOG(AskState, LogLevel::Warning, "ask: worker busy");
		setThinkingLed(false);
		sayLine(AskLine::ThoughtsLost, Cut::AfterCurrent);
		return;
	}
	CMF_LOG(AskState, LogLevel::Info, "ask: %u B question handed over", (unsigned) wav.size());
	stage = Stage::Asking;
}

AskState::~AskState(){
	// Normally the worker is done already: tick() leaves only once it is. This is the backstop for any other
	// way out (BBStateMachine::interrupt()). The worker reads the capture buffer until it is idle, so the state
	// never lets go before that (C7), however long it takes: the abort is sticky and AskClient notices it
	// within about 0.5 s, or once its connect deadline has passed during DNS and connect.
	const AskWorker* worker = ServiceLocator::AskWorkerInstance;
	if(worker != nullptr && worker->busy()){
		AskClient::abortInFlight();
		while(!worker->waitIdle(ExitWaitMs)){
			CMF_LOG(AskState, LogLevel::Warning, "ask worker still busy, waiting another %d ms", (int) ExitWaitMs);
		}
	}
	setThinkingLed(false);
	if(!networkLogged){
		VoiceTurnHeap::log(VoiceTurnHeap::Point::Network, HeapSnapshot{});
	}
	VoiceTurnHeap::endTurn();
}

void AskState::tick(float deltaTime){
	if(sm->isTransitionPending()) return;

	switch(stage){
		case Stage::Asking:
			if(ServiceLocator::AskWorkerInstance->busy()){
				// The filler isn't the answer: blink until the reply itself is heard (#10).
				if(thinkingLed && ServiceLocator::AskWorkerInstance->replyAudible()){
					setThinkingLed(false);
				}
				return;
			}
			onWorkerDone();
			return;

		case Stage::Speaking:
			if(Audio* audio = audioService(); audio != nullptr && audio->isPlaying()) return;
			stage = Stage::Leaving;
			if(rcRequested){
				sm->transitionTo<RCState>();
			}else{
				sm->transitionTo<IdleState>();
			}
			return;

		case Stage::Leaving:
			return;
	}
}

void AskState::onWorkerDone(){
	stage = Stage::Speaking;
	setThinkingLed(false);

	const AskWorker& worker = *ServiceLocator::AskWorkerInstance;
	VoiceTurnHeap::log(VoiceTurnHeap::Point::Network, worker.phaseHeap().connected);
	networkLogged = true;

	if(cancelled) return; // the abort phrase is already playing

	const AskLine line = lineFor(true, worker.result());
	if(line == AskLine::DidntUnderstand){
		sendPhase(ListenData::Phase::NotUnderstand);
	}
	sayLine(line, Cut::AfterCurrent); // after the filler if a quick error beat it
}

void AskState::cancel(bool sayAbort){
	if(cancelled.exchange(true)) return;

	AskClient::abortInFlight();
	if(Audio* audio = audioService()){
		audio->stop(); // also drops a reply queued behind the filler
	}
	if(sayAbort){
		sayPhrase(Phrase::ListenAbort, Cut::Now);
	}
	sendPhase(ListenData::Phase::Abort);
}

void AskState::sendPhase(ListenData::Phase phase){
	if(com == nullptr) return;
	ListenData data{.phase = phase};
	com->sendData(BB::State::Listen, BB::Action::Listen{}, data);
}

void AskState::onButtonEvent(int button, ISRButtonInput::Action action){
	if(button == static_cast<int>(Button::Power) && action == ISRButtonInput::Action::Press){
		cancel(true);
	}
}

void AskState::onCommand(Ctrl::Command cmd){
	if(cmd == Ctrl::Command::Listen || cmd == Ctrl::Command::ShutUp){
		cancel(true);
	}
}

void AskState::onRCData(const RCData& data){
	if(ServiceLocator::ControllerStateInstance){
		ServiceLocator::ControllerStateInstance->updateRCData(data);
	}
	// RCState aborts the turn too, but the worker has to return before the radio switches networks.
	rcRequested = true;
	cancel(false);
}
