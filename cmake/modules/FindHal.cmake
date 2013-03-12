
if(HAL_INCLUDE_DIR AND HAL_LIBRARY AND HAL_STORAGE_LIBRARY)
	# Already in cache, be silent
	set(HAL_FIND_QUIETLY TRUE)	
endif(HAL_INCLUDE_DIR AND HAL_LIBRARY AND HAL_STORAGE_LIBRARY)

set(HAL_LIBRARY)
set(HAL_INCLUDE_DIR)
set(HAL_STORAGE_LIBRARY)

FIND_PATH(HAL_INCLUDE_DIR hal/libhal.h
	/usr/include
	/usr/local/include
)

FIND_LIBRARY(HAL_LIBRARY NAMES hal
	PATHS
	/usr/lib
	/usr/local/lib
)

FIND_LIBRARY(HAL_STORAGE_LIBRARY NAMES hal-storage
	PATHS
	/usr/lib
	/usr/local/lib
)

if(HAL_INCLUDE_DIR AND HAL_LIBRARY AND HAL_STORAGE_LIBRARY)
   MESSAGE( STATUS "hal found: includes in ${HAL_INCLUDE_DIR}, library in ${HAL_LIBRARY}")
   set(HAL_FOUND TRUE)
else(HAL_INCLUDE_DIR AND HAL_LIBRARY AND HAL_STORAGE_LIBRARY)
   MESSAGE( STATUS "hal not found")
endif(HAL_INCLUDE_DIR AND HAL_LIBRARY AND HAL_STORAGE_LIBRARY)

MARK_AS_ADVANCED(HAL_INCLUDE_DIR HAL_LIBRARY HAL_STORAGE_LIBRARY)
