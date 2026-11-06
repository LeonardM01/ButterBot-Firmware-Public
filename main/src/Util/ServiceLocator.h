#ifndef BUTTERBOT_FIRMWARE_SERVICELOCATOR_H
#define BUTTERBOT_FIRMWARE_SERVICELOCATOR_H

#include <memory>
#include "Services/ControllerState.h"
#include "Services/IRStorage.h"
#include "Services/LEDControl.h"
#include "Services/ObjDet.h"
#include "Services/ScenarioRoutineService.h"
#include "Services/Settings.h"
#include "Devices/SC7A20.h"
#include "Audio/SpeechGen.h"
#include "Audio/SpeechAudioGen.h"
#include "Audio/MicInput.h"
#include "Services/WiFi/WiFiService.h"
#include "Audio/VoiceCapture.h"
#include "Net/AskWorker.h"

class ServiceLocator{
public:
	inline static std::unique_ptr<ControllerState> ControllerStateInstance = nullptr;

	inline static std::unique_ptr<IRStorage> IRStorageInstance = nullptr;

	inline static std::unique_ptr<LEDControl> LEDControlInstance = nullptr;

	inline static std::unique_ptr<ObjDet> ObjDetInstance = nullptr;

	inline static std::unique_ptr<ScenarioRoutineService> ScenarioRoutineServiceInstance = nullptr;

	inline static std::unique_ptr<Settings> SettingsInstance = nullptr;

	inline static std::unique_ptr<SC7A20> SC7A20Instance = nullptr;

	inline static std::unique_ptr<SpeechGen> SpeechGenInstance = nullptr;

	inline static std::unique_ptr<SpeechAudioGen> SpeechAudioGenInstance = nullptr;

	inline static std::unique_ptr<MicInput> MicInputInstance = nullptr;

	// Owns the radio for the whole runtime: created once at boot, never deleted (see WiFiService.h).
	inline static WiFiService* WiFiServiceInstance = nullptr;

	// Voice turn (#22): the PSRAM capture buffer and the ask worker, both created once at boot and kept.
	// Either may be null if its allocation failed; the voice turn then falls back to its spoken lines.
	inline static std::unique_ptr<VoiceCapture> VoiceCaptureInstance = nullptr;

	inline static std::unique_ptr<AskWorker> AskWorkerInstance = nullptr;
};

#endif //BUTTERBOT_FIRMWARE_SERVICELOCATOR_H