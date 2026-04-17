add_executable(ggpo_protocol_tests
	protocol_tests.cpp
)

target_include_directories(ggpo_protocol_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src/include
	${PROJECT_SOURCE_DIR}/src/lib/ggpo
	${PROJECT_SOURCE_DIR}/src/lib/ggpo/network
)

add_library(ggpo_protocol_test_support STATIC
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/bitvector.cpp
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/game_input.cpp
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/log.cpp
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/poll.cpp
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/timesync.cpp
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/network/udp.cpp
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/network/udp_proto.cpp
)

target_include_directories(ggpo_protocol_test_support PRIVATE
	${PROJECT_SOURCE_DIR}/src/include
	${PROJECT_SOURCE_DIR}/src/lib/ggpo
	${PROJECT_SOURCE_DIR}/src/lib/ggpo/network
)

if(WIN32)
	target_sources(ggpo_protocol_test_support PRIVATE
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/platform_windows.cpp
	)
	target_link_libraries(ggpo_protocol_test_support PRIVATE ws2_32 winmm)
else()
	target_sources(ggpo_protocol_test_support PRIVATE
		${PROJECT_SOURCE_DIR}/src/lib/ggpo/platform_linux.cpp
	)
endif()

target_link_libraries(ggpo_protocol_tests PRIVATE ggpo_protocol_test_support)

add_test(NAME ggpo_protocol_tests COMMAND ggpo_protocol_tests)

add_executable(ggpo_integration_tests
	integration_tests.cpp
)

target_include_directories(ggpo_integration_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src/lib/ggpo
)

target_link_libraries(ggpo_integration_tests PRIVATE GGPO)

add_test(NAME ggpo_integration_tests COMMAND ggpo_integration_tests)
set_tests_properties(ggpo_integration_tests PROPERTIES TIMEOUT 60)
