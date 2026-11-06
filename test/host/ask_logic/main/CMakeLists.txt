# The code under test is compiled straight from the firmware's main/src/Net, Audio, States and Util.
set(NET "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Net")
set(AUDIO "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Audio")
set(STATES "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/States")
set(UTIL "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Util")

idf_component_register(SRCS "test_main.cpp"
                            "test_ask_outcome.cpp"
                            "test_url_policy.cpp"
                            "test_wav_stream_parser.cpp"
                            "test_sample_aligner.cpp"
                            "test_turn_abort.cpp"
                            "test_ask_lines.cpp"
                            "test_wav_header.cpp"
                            "test_utterance_recorder.cpp"
                            "test_transition_slot.cpp"
                            "test_voice_turn_gate.cpp"
                            "${NET}/AskOutcome.cpp"
                            "${NET}/AskLines.cpp"
                            "${NET}/WavHeader.cpp"
                            "${NET}/WavStreamParser.cpp"
                            "${NET}/SampleAligner.cpp"
                            "${AUDIO}/UtteranceRecorder.cpp"
                            "${UTIL}/VoiceTurnGate.cpp"
                       INCLUDE_DIRS "${NET}" "${AUDIO}" "${STATES}" "${UTIL}"
                       REQUIRES unity)
