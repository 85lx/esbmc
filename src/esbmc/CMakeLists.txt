add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/buildidobj.txt
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/buildidobj.sh ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS main.cpp esbmc_parseoptions.cpp bmc.cpp globals.cpp document_subgoals.cpp show_vcc.cpp options.cpp ansicfrontend cppfrontend clangcfrontend clangcppfrontend symex pointeranalysis langapi util_esbmc bigint solvers clibs # Depends on... everything else linked into esbmc. Add more as necessary.
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating ESBMC version ID"
  VERBATIM
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/buildidobj.c
  COMMAND sh -c "${CMAKE_SOURCE_DIR}/scripts/flail.sh buildidstring ${CMAKE_CURRENT_BINARY_DIR}/buildidobj.txt > ${CMAKE_CURRENT_BINARY_DIR}/buildidobj.c"
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/buildidobj.txt
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Flailing build ID object"
  VERBATIM
)

add_executable (esbmc main.cpp esbmc_parseoptions.cpp bmc.cpp globals.cpp document_subgoals.cpp show_vcc.cpp options.cpp ${CMAKE_CURRENT_BINARY_DIR}/buildidobj.c)
target_include_directories(esbmc
    PRIVATE ${CMAKE_BINARY_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${Boost_INCLUDE_DIRS}
)
target_link_libraries(esbmc ansicfrontend cppfrontend clangcfrontend clangcppfrontend symex pointeranalysis langapi util_esbmc bigint solvers clibs)

install(TARGETS esbmc DESTINATION bin)
