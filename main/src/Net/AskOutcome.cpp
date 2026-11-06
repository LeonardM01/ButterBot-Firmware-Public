#include "AskOutcome.h"

AskOutcome outcomeForStatus(int status){
	switch(status){
		case 200:
			return AskOutcome::Play;
		case 204:
			return AskOutcome::DidntUnderstand;
		default:
			return AskOutcome::ThoughtsLost;
	}
}
