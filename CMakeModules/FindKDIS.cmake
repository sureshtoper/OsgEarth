FIND_PATH(KDIS_INCLUDE_DIR KDefines.h
  PATHS
  /usr/local/include
  /usr/include
  /opt/include
)

FIND_LIBRARY(KDIS_LIBRARY
  NAMES libKDIS_LIB
  PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /usr/local
    /usr
    /sw
    /opt/local
    /opt/csw
    /opt
    /usr/freeware
  PATH_SUFFIXES lib64 lib
)

SET(KDIS_FOUND "NO")
IF(KDIS_LIBRARY AND KDIS_INCLUDE_DIR)
  SET(KDIS_FOUND "YES")
ENDIF(KDIS_LIBRARY AND KDIS_INCLUDE_DIR)

