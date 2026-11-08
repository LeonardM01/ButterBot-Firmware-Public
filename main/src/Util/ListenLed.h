#ifndef BUTTERBOT_FIRMWARE_LISTENLED_H
#define BUTTERBOT_FIRMWARE_LISTENLED_H

/**
 * The status LED's fast "listening / thinking" blink (LEDControl::startListen / stopListen). A no-op until
 * LEDControl exists. Any task: LEDControl takes its own lock.
 */
void setListenLed(bool on);

#endif //BUTTERBOT_FIRMWARE_LISTENLED_H
