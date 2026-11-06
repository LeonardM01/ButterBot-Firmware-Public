#ifndef BUTTERBOT_FIRMWARE_ASKOUTCOME_H
#define BUTTERBOT_FIRMWARE_ASKOUTCOME_H

/**
 * What the robot does after a /v1/ask turn (#7 point 7). Outcomes come from the
 * HTTP status code only; the robot never parses an error body.
 */
enum class AskOutcome {
	Play,            // 200: play the reply audio
	DidntUnderstand, // 204: no speech or an empty transcript
	ThoughtsLost,    // anything else, including transport errors and timeouts
};

/**
 * Maps an HTTP status to an outcome. Pass -1 when no status line arrived
 * (connect, TLS or DNS failure, timeout), as esp_http_client reports it.
 */
AskOutcome outcomeForStatus(int status);

#endif //BUTTERBOT_FIRMWARE_ASKOUTCOME_H
