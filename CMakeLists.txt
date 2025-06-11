CU_SET_PATH("CMAKE_GUILD_POINTS_DIR" "${CMAKE_CURRENT_LIST_DIR}")

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/GuildPoints.cpp")
AC_ADD_SCRIPT_LOADER("GuildPoints" "${CMAKE_CURRENT_LIST_DIR}/src/loader.h")

AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/GuildPoints.conf.dist")

include_directories(
  ${CMAKE_SOURCE_DIR}/src/server/game
  ${CMAKE_SOURCE_DIR}/src/server/shared
  ${CMAKE_SOURCE_DIR}/src/server/database
  ${CMAKE_CURRENT_LIST_DIR}/src
) 