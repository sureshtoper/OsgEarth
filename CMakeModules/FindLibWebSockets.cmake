FIND_PATH(LIBWEBSOCKETS_INCLUDE_DIR libwebsockets.h
  PATHS
  /usr/local/include
  /usr/include
  /opt/include
)

FIND_LIBRARY(LIBWEBSOCKETS_LIBRARY
  NAMES libwebsocketswin32 libwebsockets
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

SET(LIBWEBSOCKETS_FOUND "NO")
IF(LIBWEBSOCKETS_LIBRARY AND LIBWEBSOCKETS_INCLUDE_DIR)
  SET(LIBWEBSOCKETS_FOUND "YES")
ENDIF(LIBWEBSOCKETS_LIBRARY AND LIBWEBSOCKETS_INCLUDE_DIR)

