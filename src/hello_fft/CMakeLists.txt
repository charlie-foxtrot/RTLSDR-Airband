set(hello_fft_source_files
	mailbox.c
	gpu_fft.c
	gpu_fft_twiddles.c
	gpu_fft_shaders.c
	gpu_fft_base.c
)
# Temp hack due to the fact that mailbox.c includes ../rtl_airband.h which
# is a C++ header.
SET_SOURCE_FILES_PROPERTIES(${hello_fft_source_files} PROPERTIES LANGUAGE CXX )
add_library(hello_fft OBJECT
	${hello_fft_source_files}
)
target_include_directories(hello_fft PUBLIC
	".."
	${BCM_VC_INCLUDE_DIRS}
)
