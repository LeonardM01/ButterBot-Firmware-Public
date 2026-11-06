# The code under test is compiled straight from the firmware's main/src/Debug.
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../../main/src")

idf_component_register(SRCS "test_main.cpp"
                            "test_power_press.cpp"
                            "test_measure_format.cpp"
                            "test_afe_glitch_meter.cpp"
                            "${SRC}/Debug/PowerPress.cpp"
                            "${SRC}/Debug/MeasureFormat.cpp"
                            "${SRC}/Debug/AfeGlitchMeter.cpp"
                       INCLUDE_DIRS "${SRC}/Debug" "${SRC}"
                       REQUIRES unity)
