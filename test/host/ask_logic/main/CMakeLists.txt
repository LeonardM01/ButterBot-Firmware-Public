# The code under test is compiled straight from the firmware's main/src/Net and main/src/Audio.
set(NET "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Net")
set(AUDIO "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Audio")

idf_component_register(SRCS "test_main.cpp"
                            "test_ask_outcome.cpp"
                            "test_url_policy.cpp"
                            "test_wav_stream_parser.cpp"
                            "test_sample_aligner.cpp"
                            "test_turn_abort.cpp"
                            "test_ask_lines.cpp"
                            "test_wav_header.cpp"
                            "test_utterance_recorder.cpp"
                            "${NET}/AskOutcome.cpp"
                            "${NET}/AskLines.cpp"
                            "${NET}/WavHeader.cpp"
                            "${NET}/WavStreamParser.cpp"
                            "${NET}/SampleAligner.cpp"
                            "${AUDIO}/UtteranceRecorder.cpp"
                       INCLUDE_DIRS "${NET}" "${AUDIO}"
                       REQUIRES unity)
