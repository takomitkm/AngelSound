add_executable(test_wav test_wav.c)
target_link_libraries(test_wav PRIVATE angelsound_common)
add_test(NAME wav COMMAND test_wav)

add_executable(test_practice test_practice.c)
target_link_libraries(test_practice PRIVATE angelsound_common)
add_test(NAME practice COMMAND test_practice)