# The code under test is compiled straight from the firmware's main/src/Net.
set(NET "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src/Net")

idf_component_register(SRCS "test_main.cpp"
                            "test_ask_outcome.cpp"
                            "${NET}/AskOutcome.cpp"
                       INCLUDE_DIRS "${NET}"
                       REQUIRES unity)
