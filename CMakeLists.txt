cmake_minimum_required(VERSION 3.15)
project(GrinningSoul)

add_subdirectory("${PROJECT_SOURCE_DIR}/Converter" "${PROJECT_SOURCE_DIR}/converter_output")
add_subdirectory("${PROJECT_SOURCE_DIR}/HeaderParser" "${PROJECT_SOURCE_DIR}/headerparser_output")
add_subdirectory("${PROJECT_SOURCE_DIR}/ArmRuntime" "${PROJECT_SOURCE_DIR}/armruntime_output")
add_subdirectory("${PROJECT_SOURCE_DIR}/Runtime" "${PROJECT_SOURCE_DIR}/runtime_output")

