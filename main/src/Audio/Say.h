#ifndef BUTTERBOT_FIRMWARE_SAY_H
#define BUTTERBOT_FIRMWARE_SAY_H

#include <cstdint>
#include <string>
#include <Phrases.h>

/**
 * Short spoken lines through flite (SpeechAudioGen), shared by the listen and ask states. Every call is a no-op
 * when the text is empty or Audio or flite is missing. State machine task.
 */
namespace Say {

enum class Cut : uint8_t {
	Now,          // stops whatever plays, a queued reply included
	AfterCurrent, // queued behind what plays (a filler is never cut, decisions #5)
};

void text(const std::string& text, Cut cut);

/** Says `text`, cutting whatever plays, and returns once it has finished. */
void textBlocking(const std::string& text);

/** A random variant of `phrase` (Phrases::get). */
void phrase(Phrase phrase, Cut cut);

void phraseBlocking(Phrase phrase);

}

#endif //BUTTERBOT_FIRMWARE_SAY_H
