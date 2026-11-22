add_executable(test_wav test_wav.c)
target_link_libraries(test_wav PRIVATE angelsound_common)
add_test(NAME wav COMMAND test_wav)

add_executable(test_practice test_practice.c)
target_link_libraries(test_practice PRIVATE angelsound_common)
add_test(NAME practice COMMAND test_practice)

add_executable(test_recordlist test_recordlist.c)
target_link_libraries(test_recordlist PRIVATE angelsound_common)
add_test(NAME recordlist COMMAND test_recordlist)

add_executable(test_storage_pure test_storage_pure.c)
target_link_libraries(test_storage_pure PRIVATE angelsound_common)
add_test(NAME storage_pure COMMAND test_storage_pure)

add_executable(test_ui_layout test_ui_layout.c)
target_link_libraries(test_ui_layout PRIVATE angelsound_common)
add_test(NAME ui_layout COMMAND test_ui_layout)

add_executable(test_audio_format test_audio_format.c)
target_link_libraries(test_audio_format PRIVATE angelsound_audio)
add_test(NAME audio_format COMMAND test_audio_format)