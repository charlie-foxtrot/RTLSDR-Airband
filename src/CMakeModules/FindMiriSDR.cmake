# - Try to find mirisdr - the hardware driver for Mirics chip in the dvb receivers
# Once done this will define
#  MIRISDR_FOUND - System has mirisdr
#  MIRISDR_LIBRARIES - The mirisdr libraries
#  MIRISDR_INCLUDE_DIRS - The mirisdr include directories
#  MIRISDR_LIB_DIRS - The mirisdr library directories

if(NOT MIRISDR_FOUND)

    find_package(PkgConfig)
    pkg_check_modules (MIRISDR_PKG libmirisdr)
    set(MIRISDR_DEFINITIONS ${PC_MIRISDR_CFLAGS_OTHER})

    find_path(MIRISDR_INCLUDE_DIR
                NAMES mirisdr.h
                HINTS ${MIRISDR_PKG_INCLUDE_DIRS} $ENV{MIRISDR_DIR}/include
                PATHS /usr/local/include /usr/include /opt/include /opt/local/include)

    find_library(MIRISDR_LIBRARY
                NAMES mirisdr
                HINTS ${MIRISDR_PKG_LIBRARY_DIRS} $ENV{MIRISDR_DIR}/include
                PATHS /usr/local/lib /usr/lib /opt/lib /opt/local/lib)

    set(MIRISDR_LIBRARIES ${MIRISDR_LIBRARY} )
    set(MIRISDR_INCLUDE_DIRS ${MIRISDR_INCLUDE_DIR} )

    include(FindPackageHandleStandardArgs)
    # handle the QUIETLY and REQUIRED arguments and set LibMIRISDR_FOUND to TRUE
    # if all listed variables are TRUE
    find_package_handle_standard_args(MiriSDR  DEFAULT_MSG
                                      MIRISDR_LIBRARY MIRISDR_INCLUDE_DIR)

    mark_as_advanced(MIRISDR_INCLUDE_DIR MIRISDR_LIBRARY)

endif(NOT MIRISDR_FOUND)
