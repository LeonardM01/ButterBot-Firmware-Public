#include "Say.h"
#include <memory>
#include <Services/Audio/Audio.h>
#include <Statics/ApplicationStatics.h>
#include <Core/Application.h>
#include <Util/ServiceLocator.h>
#include "SpeechAudioSource.h"

namespace {

Audio* audioFor(const std::string& text){
	if(text.empty() || !ServiceLocator::SpeechAudioGenInstance || !ServiceLocator::SpeechGenInstance) return nullptr;
	const Application* app = ApplicationStatics::getApplication();
	return app != nullptr ? app->getService<Audio>() : nullptr;
}

std::string phraseText(Phrase phrase){
	const int16_t id = Phrases::get(phrase);
	return id >= 0 ? Phrases::map(phrase, id) : std::string{};
}

}

void Say::text(const std::string& text, Cut cut){
	Audio* audio = audioFor(text);
	if(audio == nullptr) return;
	auto source = std::make_unique<SpeechAudioSource>(SpeechGen::InputType::Text, text);
	if(cut == Cut::Now){
		audio->play(ServiceLocator::SpeechAudioGenInstance.get(), std::move(source));
	}else{
		audio->enqueue(ServiceLocator::SpeechAudioGenInstance.get(), std::move(source));
	}
}

void Say::textBlocking(const std::string& text){
	Audio* audio = audioFor(text);
	if(audio == nullptr) return;
	audio->play(ServiceLocator::SpeechAudioGenInstance.get(), std::make_unique<SpeechAudioSource>(SpeechGen::InputType::Text, text));
	audio->waitEnd(portMAX_DELAY);
}

void Say::phrase(Phrase phrase, Cut cut){
	text(phraseText(phrase), cut);
}

void Say::phraseBlocking(Phrase phrase){
	textBlocking(phraseText(phrase));
}
