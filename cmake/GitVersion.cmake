# Stamp the build with the commit it was actually built from.
#
# This runs with cmake -P on every build, not once at configure time. The
# configure-time version of this went stale the moment anything was committed
# after the build directory was made, and then reported a commit the binary had
# not contained for days - which is worse than reporting nothing, because it
# looks like an answer.
#
# The header is written to a temporary file and copied only if it differs, so a
# build that has not moved does not recompile system.c.

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${SRC_DIR}
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${SRC_DIR}
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

# Uncommitted changes mean the hash is only half the truth. Say so, because a
# build from a dirty tree is the one most likely to be mistaken for its commit.
execute_process(
  COMMAND git status --porcelain --untracked-files=no
  WORKING_DIRECTORY ${SRC_DIR}
  OUTPUT_VARIABLE VERSION_DIRTY
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(NOT VERSION_DIRTY STREQUAL "")
  set(VERSION_HASH "${VERSION_HASH}-dirty")
endif()

if(VERSION_HASH STREQUAL "")
  set(VERSION_HASH "unknown")
endif()

if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "port-custom")
endif()

configure_file("${TEMPLATE}" "${OUT_FILE}.tmp" @ONLY)

execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUT_FILE}.tmp" "${OUT_FILE}"
)

file(REMOVE "${OUT_FILE}.tmp")
