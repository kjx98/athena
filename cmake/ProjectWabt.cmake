if(ProjectWabtIncluded)
    return()
endif()
set(ProjectWabtIncluded TRUE)

include(ExternalProject)

set(prefix ${CMAKE_BINARY_DIR})
set(source_dir ${CMAKE_SOURCE_DIR}/wabt)
set(binary_dir ${CMAKE_BINARY_DIR}/wabt)
set(include_dir ${source_dir})
set(wabt_library ${binary_dir}/${CMAKE_STATIC_LIBRARY_PREFIX}wabt${CMAKE_STATIC_LIBRARY_SUFFIX})

ExternalProject_Add(wabt
    PREFIX ${prefix}
    SOURCE_DIR ${source_dir}
    BINARY_DIR ${binary_dir}
    CMAKE_ARGS
	-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE=Release
    -DWITH_EXCEPTIONS=ON
    -DBUILD_TESTS=OFF
    -DBUILD_TOOLS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_CXX_FLAGS=-fvisibility=hidden
    -DCMAKE_C_FLAGS=-fvisibility=hidden
	INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${wabt_library}
)

add_library(wabt::wabt STATIC IMPORTED)

set_target_properties(
    wabt::wabt
    PROPERTIES
    IMPORTED_CONFIGURATIONS Release
	IMPORTED_LOCATION_RELEASE ${wabt_library}
    INTERFACE_INCLUDE_DIRECTORIES "${include_dir};${binary_dir}"
)

add_dependencies(wabt::wabt wabt)
