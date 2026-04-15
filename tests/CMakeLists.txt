add_executable(ggpo_protocol_tests
	protocol_tests.cpp
)

target_include_directories(ggpo_protocol_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src/lib/ggpo
	${PROJECT_SOURCE_DIR}/src/lib/ggpo/network
)

target_link_libraries(ggpo_protocol_tests PRIVATE GGPO)

add_test(NAME ggpo_protocol_tests COMMAND ggpo_protocol_tests)
