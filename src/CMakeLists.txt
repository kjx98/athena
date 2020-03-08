find_package(Threads REQUIRED)

#hunter_add_package(intx)
#find_package(intx CONFIG REQUIRED)

set(athena_include_dir ${PROJECT_SOURCE_DIR}/include)
get_filename_component(evmc_include_dir .. ABSOLUTE)

add_library(athena
    debugging.h
    ${athena_include_dir}/athena/athena.h
    eei.cpp
    eei.h
    helpers.cpp
    helpers.h
    athena.cpp
)

if(H_WABT)
  target_sources(athena PRIVATE wabt.cpp wabt.h)
endif()

if(H_EOS)
  target_sources(athena PRIVATE eosvm.cpp eosvm.h)
endif()

option(H_DEBUGGING "Display debugging messages during execution." ON)
if(H_DEBUGGING)
  target_compile_definitions(athena PRIVATE H_DEBUGGING=1)
endif()

target_include_directories(athena
    PUBLIC $<BUILD_INTERFACE:${athena_include_dir}>$<INSTALL_INTERFACE:include>
)
target_link_libraries(athena PUBLIC evmc::evmc PRIVATE athena-buildinfo evmc::instructions )
if(NOT WIN32)
  if(CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(athena PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_target_properties(athena PROPERTIES LINK_FLAGS "-Wl,-undefined,error")
  endif()
endif()

if(H_WABT)
    target_compile_definitions(athena PRIVATE H_WABT=1)
    target_link_libraries(athena PRIVATE wabt::wabt)
endif()

if(H_WAVM)
    target_compile_definitions(athena PRIVATE H_WAVM=1)
    target_link_libraries(athena PRIVATE wavm::wavm)
endif()

install(TARGETS athena EXPORT athenaTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
