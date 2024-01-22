set (VERSION "char const *RTL_AIRBAND_VERSION=\"${RTL_AIRBAND_VERSION}\";\n")

if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)
	file(READ ${CMAKE_CURRENT_BINARY_DIR}/version.cpp VERSION_)
else()
	set(VERSION_ "")
endif()

if (NOT "${VERSION}" STREQUAL "${VERSION_}")
	file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version.cpp "${VERSION}")
endif()
