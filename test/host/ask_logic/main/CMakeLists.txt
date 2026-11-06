# The code under test is compiled straight from the firmware's main/src/Net.
set(NET "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Net")

idf_component_register(SRCS "test_main.cpp"
                            "test_ask_outcome.cpp"
                            "test_url_policy.cpp"
                            "test_wav_stream_parser.cpp"
                            "test_sample_aligner.cpp"
                            "test_turn_abort.cpp"
                            "${NET}/AskOutcome.cpp"
                            "${NET}/WavStreamParser.cpp"
                            "${NET}/SampleAligner.cpp"
                       INCLUDE_DIRS "${NET}"
                       REQUIRES unity)
