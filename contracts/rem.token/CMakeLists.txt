add_contract(rem.token rem.token ${CMAKE_CURRENT_SOURCE_DIR}/src/rem.token.cpp)

target_include_directories(rem.token
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(rem.token
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/rem.token.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/rem.token.contracts.md @ONLY )

target_compile_options( rem.token PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
