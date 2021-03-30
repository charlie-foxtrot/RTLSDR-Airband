if(NOT BCM_VC_FOUND)

	set(BCM_VC_PATH "/opt/vc" CACHE STRING "List of paths to search for Broadcom VideoCore library")

	find_path(BCM_VC_INCLUDE_DIR bcm_host.h PATHS ${BCM_VC_PATH}/include)
	find_library(BCM_VC_LIBRARY NAMES bcm_host PATHS ${BCM_VC_PATH}/lib)

	set(BCM_VC_LIBRARIES ${BCM_VC_LIBRARY} )
	set(BCM_VC_INCLUDE_DIRS ${BCM_VC_INCLUDE_DIR} )

	include(FindPackageHandleStandardArgs)
	# handle the QUIETLY and REQUIRED arguments and set BCM_VC_FOUND to TRUE
	# if all listed variables are TRUE
	find_package_handle_standard_args(BCM_VC DEFAULT_MSG
					  BCM_VC_LIBRARY BCM_VC_INCLUDE_DIR)

	mark_as_advanced(BCM_VC_INCLUDE_DIR BCM_VC_LIBRARY)

endif()
