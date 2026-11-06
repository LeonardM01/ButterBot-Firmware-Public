#include "ListenState.h"
#include "BBStateMachine.h"
#include "Audio/AudioFrontend.h"
#include "Audio/Say.h"
#include "Audio/SpeechAudioGen.h"
#include "Audio/SpeechAudioSource.h"
#include "Audio/SpeechGen.h"
#include <Core/Application.h>
#include <Statics/ApplicationStatics.h>
#include <Phrases.h>
#include <Services/Audio/Audio.h>
#include <Services/Audio/AACAudioGenerator.h>
#include "Audio/MemoryAudioSource.h"
#include "Util/PSRAMAllocator.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <vector>
#include <Util/ServiceLocator.h>
#include "BBData.h"
#include "Enums.h"
#include "AskState.h"
#include "IdleState.h"
#include "RCState.h"
#include "ScenarioState.h"
#include "Services/Com.h"
#include "Services/ScenarioRoutineService.h"
#include "Services/IRStorage.h"
#include "Routines/IRActionRoutine.h"
#include "ScenarioRoutineMappings.h"
#include "Net/AskLines.h"
#include "Net/AskWorker.h"
#include "Audio/VoiceCapture.h"
#include "Util/VoiceTurnGate.h"
#include "Util/VoiceTurnHeap.h"

DEFINE_LOG(ListenState)

static const char* TAG = "ListenState";

namespace {
    constexpr uint32_t IntroWindowMs = 2500;

    struct SoundEffect { const char* path; uint16_t durationMs; };
    struct StartPrompt { const char* text; uint16_t durationMs; };

    constexpr StartPrompt StartPrompts[] = {
        { "Yes?",                  753 },
        { "I'm listening",         978 },
        { "How can I help?",      1303 },
        { "Another task?",        1010 },
        { "How may I assist?",    1417 },
        { "You have my attention", 1531 },
        { "Awaiting input",       1144 },
        { "State your request",   1511 },
    };

    constexpr SoundEffect Lasers1[] = {
        { "/spiffs/listen/lasers1/phaseJump1.aac",  384 },
        { "/spiffs/listen/lasers1/phaseJump2.aac",  384 },
        { "/spiffs/listen/lasers1/phaseJump3.aac",  320 },
        { "/spiffs/listen/lasers1/phaseJump4.aac",  256 },
        { "/spiffs/listen/lasers1/phaseJump5.aac",  384 },
        { "/spiffs/listen/lasers1/phaserDown1.aac", 384 },
        { "/spiffs/listen/lasers1/phaserDown2.aac", 320 },
        { "/spiffs/listen/lasers1/phaserDown3.aac", 384 },
    };

    constexpr SoundEffect Lasers2[] = {
        { "/spiffs/listen/lasers2/laser1.aac", 384 },
        { "/spiffs/listen/lasers2/laser2.aac", 384 },
        { "/spiffs/listen/lasers2/laser3.aac", 384 },
        { "/spiffs/listen/lasers2/laser4.aac", 384 },
        { "/spiffs/listen/lasers2/laser5.aac", 384 },
        { "/spiffs/listen/lasers2/laser6.aac", 320 },
        { "/spiffs/listen/lasers2/laser7.aac", 384 },
        { "/spiffs/listen/lasers2/laser8.aac", 384 },
        { "/spiffs/listen/lasers2/laser9.aac", 384 },
    };

    constexpr SoundEffect Robot[] = {
        { "/spiffs/listen/robot/robot_1.aac", 256 },
        { "/spiffs/listen/robot/robot_2.aac", 384 },
        { "/spiffs/listen/robot/robot_3.aac", 320 },
        { "/spiffs/listen/robot/robot_4.aac", 256 },
    };

    struct SfxGroup { const char* name; std::span<const SoundEffect> clips; };
    constexpr SfxGroup SfxGroups[] = {
        { "lasers1", Lasers1 },
        { "lasers2", Lasers2 },
        { "robot",   Robot   },
    };

    PSRAMByteBuffer readFile(const char* path){
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if(!f){
            return {};
        }
        const std::streamsize size = f.tellg();
        if(size <= 0){
            return {};
        }
        f.seekg(0);
        PSRAMByteBuffer buf(static_cast<size_t>(size));
        f.read(reinterpret_cast<char*>(buf.data()), size);
        buf.resize(static_cast<size_t>(f.gcount()));
        return buf;
    }

    void buildAndPlayIntro(const Application* app){
        Audio* audio = app->getService<Audio>();
        AACAudioGenerator* aacGen = app->getService<AACAudioGenerator>();
        if(audio == nullptr || aacGen == nullptr || ServiceLocator::SpeechAudioGenInstance == nullptr || ServiceLocator::SpeechGenInstance == nullptr){
            return;
        }

        const StartPrompt& prompt = StartPrompts[rand() % std::size(StartPrompts)];
        const uint32_t budget = prompt.durationMs >= IntroWindowMs ? 0 : IntroWindowMs - prompt.durationMs;

        const SfxGroup& group = SfxGroups[rand() % std::size(SfxGroups)];

        const uint32_t preloadStart = millis();

        const size_t maxAttempts = group.clips.size() * 4;

        std::vector<Audio::GenSourcePair> batch;
        batch.reserve(maxAttempts + 1); // + 1 for the spoken prompt source

        std::vector<size_t> pool;
        pool.reserve(group.clips.size());
        uint32_t accumulated = 0;
        size_t attempts = 0;
        while(accumulated < budget && attempts < maxAttempts){
            ++attempts;
            if(pool.empty()){
                for(size_t i = 0; i < group.clips.size(); ++i){
                    pool.push_back(i);
                }
            }
            const size_t poolIdx = rand() % pool.size();
            const SoundEffect& sfx = group.clips[pool[poolIdx]];
            pool.erase(pool.begin() + poolIdx);

            // Preload the clip into PSRAM now (before loadModel() hammers the flash),
            // so the audio task can play it without blocking on the flash lock.
            PSRAMByteBuffer bytes = readFile(sfx.path);
            if(bytes.empty()){
                continue;
            }

            batch.emplace_back(aacGen, std::make_unique<MemoryAudioSource>(std::move(bytes)));
            accumulated += sfx.durationMs;
        }

        batch.emplace_back(ServiceLocator::SpeechAudioGenInstance.get(), std::make_unique<SpeechAudioSource>(SpeechGen::InputType::Text, prompt.text));

        audio->play(std::span<Audio::GenSourcePair>(batch));
    }

}

std::vector<AudioFrontend::Phrase> ListenState::phrases;

void ListenState::preallocate(){
	phrases.reserve(activations.size() * 2);
}

ListenState::ListenState(BBStateMachine* sm) : State(sm){
	phrases.clear();

    const Application* app = ApplicationStatics::getApplication();

    // Two refusals (VoiceTurnGate), both spoken as "thoughts got lost" by the destructor, then Idle:
    // - the ask worker still reads the capture buffer, so recording now would overwrite its upload (C7). Only
    //   this task can make the worker busy again (submit()), so the check holds for the whole constructor;
    // - too little internal RAM for AFE + MultiNet, whose load would reboot the robot (decisions #9).
    const AskWorker* worker = ServiceLocator::AskWorkerInstance;
    const HeapSnapshot idle = VoiceTurnHeap::beginTurn();
    switch(VoiceTurnGate::check(worker != nullptr && worker->busy(), idle.intFree, idle.intLargest)){
        case VoiceTurnGate::Verdict::Go:
            break;
        case VoiceTurnGate::Verdict::AskStillRunning:
            CMF_LOG(ListenState, LogLevel::Warning, "Not listening: the last question is still being asked");
            refused = true;
            sm->transitionTo<IdleState>();
            return;
        case VoiceTurnGate::Verdict::LowInternalRam:
            CMF_LOG(ListenState, LogLevel::Warning, "Not loading the speech models: internal RAM free %u B (need %u), largest block %u B (need %u)",
                    (unsigned) idle.intFree, (unsigned) VoiceTurnGate::MinIntFree,
                    (unsigned) idle.intLargest, (unsigned) VoiceTurnGate::MinIntLargest);
            refused = true;
            sm->transitionTo<IdleState>();
            return;
    }

    buildAndPlayIntro(app);

    auto buttonInput = app->getService<ISRButtonInput>();
    if(buttonInput == nullptr){
        ESP_LOGE(TAG, "ButtonInput service missing!");
        return;
    }

    sm->bindState(buttonInput->OnButtonEvent, this, &ListenState::onButtonEvent);

    if(!ServiceLocator::ScenarioRoutineServiceInstance){
        CMF_LOG(ListenState, LogLevel::Error, "ScenarioRoutineService missing in constructor");
        return;
    }

    ServiceLocator::ScenarioRoutineServiceInstance->reset();

    com = ApplicationStatics::getApplication()->getService<Com>();
    if(com == nullptr){
        ESP_LOGE(TAG, "Com service missing!");
        return;
    }

    sm->bindState(com->OnCommand, this, &ListenState::onCommand);
    ListenData listenData{.phase = ListenData::Phase::Listening};
    com->sendData(BB::State::Listen, BB::Action::Listen{}, listenData);


    AudioFrontend* audioFrontend = app->getService<AudioFrontend>();
    if(audioFrontend == nullptr){
        CMF_LOG(ListenState, LogLevel::Error, "AudioFrontend service missing in constructor");
        return;
    }

    if(!audioFrontend->loadModel()){
        // Same refusal as the guard's: the line plays in the destructor, then Idle.
        refused = true;
        sm->transitionTo<IdleState>();
        return;
    }
    VoiceTurnHeap::mark(VoiceTurnHeap::Point::Loaded);

    audioFrontend->setMode(AudioFrontend::Mode::Off);

    if(!activations.empty()){
        for(const ScenarioActivation& activation : activations){
            const RoutineFactory factory = routineForScenario({ activation.scenario, activation.scenarioData });
            phrases.push_back({ activation.string, activation.phonemes, activation.fuzzyCore, activation.fuzzyThreshold, reinterpret_cast<const void*>(factory) });
        }

        if(ServiceLocator::IRStorageInstance){
            for(uint8_t i = 0; i < ServiceLocator::IRStorageInstance->getCount(); ++i){
                const IRCommand& cmd = ServiceLocator::IRStorageInstance->getCommand(i);
                phrases.push_back({ cmd.phonemes, cmd.phonemes });
            }
        }

        audioFrontend->setSpeechPhrases(phrases);
    }

    if(Audio* audio = app->getService<Audio>()){
        audio->waitEnd(portMAX_DELAY);

        sm->bindState(audioFrontend->onPhrase, this, &ListenState::onPhrase);
        // Record the question too, into the boot-time buffer (#22); the worker is idle (checked above).
        // setMode(Off) detaches it.
        if(VoiceCapture* capture = ServiceLocator::VoiceCaptureInstance){
            capture->begin();
            audioFrontend->setCapture(&capture->recorder());
        }
        audioFrontend->setMode(AudioFrontend::Mode::Speech);
    }
}

ListenState::~ListenState(){
    // Event bindings are dropped by BBStateMachine::unbindStateEvents() after this dtor runs.
    const Application* app = ApplicationStatics::getApplication();
    // Unloading here, before AskState's constructor runs, gives the network phase back ~17 KB of internal RAM.
    if(AudioFrontend* audioFrontend = app->getService<AudioFrontend>()){
        audioFrontend->setMode(AudioFrontend::Mode::Off);
		audioFrontend->unloadModel();
    }

    if(refused){
        Say::textBlocking(askLineText(AskLine::ThoughtsLost, (uint32_t) rand()));
    }else if(timedOut){
        Say::phraseBlocking(Phrase::ListeningTimeout);
    }

    // A question handed to AskState is still the same turn; it ends there.
    if(!askRequested){
        VoiceTurnHeap::endTurn();
    }
}

void ListenState::onRC(){
    sm->transitionTo<RCState>();
}

void ListenState::onPhrase(bool recognized, int index, const std::string& transcript, float confidence){
	// Runs on the AFE proc task. Do NOT stop the AFE or play audio here
    if(!recognized){
        // Sizes only, never the transcript (S0.5).
        VoiceCapture* capture = ServiceLocator::VoiceCaptureInstance;
        const bool question = capture != nullptr && capture->recorder().state() == UtteranceRecorder::State::Done && capture->recorder().bytes() > 0;
    	CMF_LOG(ListenState, LogLevel::Info, "Listen result: no command, question=%d, recorded %u B",
			question, capture != nullptr ? (unsigned) capture->recorder().bytes() : 0u);

        if(question){
            // Not a built-in command: ask the API (#22). The models unload in the destructor first.
            askRequested = true;
            sm->transitionTo<AskState>();
            return;
        }

        // No speech at all: today's local "didn't understand", no upload (decisions #3).
        ListenData listenData{.phase = ListenData::Phase::NotUnderstand};
        com->sendData(BB::State::Listen, BB::Action::Listen{}, listenData);
        timedOut = true;
        sm->transitionTo<IdleState>();
        return;
    }

	if(index < 0){
        // Abort phrase
        aborted();
        sm->transitionTo<IdleState>();
        return;
    }

    if(!ServiceLocator::ScenarioRoutineServiceInstance){
        CMF_LOG(ListenState, LogLevel::Error, "ScenarioRoutineService missing in onPhrase");
        return;
    }

    if(index >= static_cast<int>(activations.size())){
        const int irIndex = index - static_cast<int>(activations.size());
        if(ServiceLocator::IRStorageInstance){
            ServiceLocator::IRStorageInstance->setSelectedIndex(static_cast<uint8_t>(irIndex));
        }
        ServiceLocator::ScenarioRoutineServiceInstance->setRoutineFactory(&makeRoutine<IRActionRoutine>);
        sm->transitionTo<ScenarioState>();
        return;
    }

    const ScenarioActivation& activation = activations[index];
    const RoutineFactory factory = routineForScenario({ activation.scenario, activation.scenarioData });
    if(factory == nullptr){
        CMF_LOG(ListenState, LogLevel::Error, "No routine mapped for scenario %d", static_cast<int>(activation.scenario));
        sm->transitionTo<IdleState>();
        return;
    }

    ServiceLocator::ScenarioRoutineServiceInstance->setRoutineFactory(factory);
    sm->transitionTo<ScenarioState>();
}

void ListenState::onButtonEvent(int button, ISRButtonInput::Action action){
    if(action != ISRButtonInput::Action::Press || button != (int)Button::Power){
        return;
    }
    aborted();
    sm->transitionTo<IdleState>();
}

void ListenState::onCommand(Ctrl::Command cmd) {
    if(cmd == Ctrl::Command::Listen || cmd == Ctrl::ShutUp){
        aborted();
        sm->transitionTo<IdleState>();
    	return;
    }
}

void ListenState::aborted(){
	if(ServiceLocator::ScenarioRoutineServiceInstance){
		Say::phrase(Phrase::ListenAbort, Say::Cut::Now);
	}

    if(com == nullptr){
        return;
    }

    ListenData listenData{.phase = ListenData::Phase::Abort};
    com->sendData(BB::State::Listen, BB::Action::Listen{}, listenData);
}
