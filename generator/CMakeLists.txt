find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(generator_resources "${CMAKE_CURRENT_BINARY_DIR}/generator_resources")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/support_resources.py"
    --framework "${PSXRECOMP_ROOT}" --config "${CMAKE_CURRENT_LIST_DIR}/../recomp/game.toml"
    --output "${generator_resources}" --deps "${CMAKE_BINARY_DIR}/_deps" COMMAND_ERROR_IS_FATAL ANY)
add_executable(generate-data WIN32 main.cpp generate_data.cpp encode_cdda.cpp
    "${PSXRECOMP_ROOT}/runtime/src/iso_reader.cpp"
    "${PSXRECOMP_ROOT}/runtime/src/cue_sheet.cpp"
    ../vendor/audio/cdda_archive.cpp "${generator_resources}/support_files.rc")
target_include_directories(generate-data PRIVATE "${generator_resources}")
target_include_directories(generate-data PRIVATE "${PSXRECOMP_ROOT}/runtime/include" "${PSXRECOMP_ROOT}/recompiler/include")
target_compile_features(generate-data PRIVATE cxx_std_17)
target_compile_definitions(generate-data PRIVATE UNICODE _UNICODE)
target_link_libraries(generate-data PRIVATE bcrypt comctl32 comdlg32 shell32 ole32 uuid chdr-static rage_mp3_encoder rage_mp3_decoder)
set_target_properties(generate-data PROPERTIES OUTPUT_NAME "Generate Data")
add_executable(mp3-validation ../tests/mp3_tests.cpp encode_cdda.cpp
    "${PSXRECOMP_ROOT}/runtime/src/iso_reader.cpp" "${PSXRECOMP_ROOT}/runtime/src/cue_sheet.cpp"
    ../vendor/audio/cdda_archive.cpp)
target_include_directories(mp3-validation PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PSXRECOMP_ROOT}/runtime/include" "${PSXRECOMP_ROOT}/recompiler/include")
target_compile_features(mp3-validation PRIVATE cxx_std_17)
target_link_libraries(mp3-validation PRIVATE chdr-static rage_mp3_encoder rage_mp3_decoder)
add_executable(music-validation ../tests/music_tests.cpp ../recomp/src/rage_music.cpp ../recomp/src/song_catalog.cpp)
target_include_directories(music-validation PRIVATE ../tests ../recomp/include "${PSXRECOMP_ROOT}/runtime/include")
target_compile_features(music-validation PRIVATE cxx_std_17)
target_compile_definitions(music-validation PRIVATE PSX_EXECUTION_PROFILE_CONFIG="music_test_profile.h")
target_link_libraries(music-validation PRIVATE rage_mp3_decoder)
if(MSVC)
    target_compile_options(generate-data PRIVATE /utf-8)
    target_link_options(generate-data PRIVATE /MANIFEST:NO)
endif()
