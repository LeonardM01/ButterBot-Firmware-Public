#include "VoiceTurnGate.h"

VoiceTurnGate::Verdict VoiceTurnGate::check(bool askWorkerBusy, size_t intFree, size_t intLargest){
	if(askWorkerBusy) return Verdict::AskStillRunning;
	if(intFree < MinIntFree || intLargest < MinIntLargest) return Verdict::LowInternalRam;
	return Verdict::Go;
}
