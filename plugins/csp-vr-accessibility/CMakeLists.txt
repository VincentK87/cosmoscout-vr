# ------------------------------------------------------------------------------------------------ #
#                                This file is part of CosmoScout VR                                #
#       and may be used under the terms of the MIT license. See the LICENSE file for details.      #
#                         Copyright: (c) 2019 German Aerospace Center (DLR)                        #
# ------------------------------------------------------------------------------------------------ #

option(CSP_VR_ACCESSIBILITY "Enable compilation of this plugin" ON)

if (NOT CSP_VR_ACCESSIBILITY)
  return()
endif()

# build plugin -------------------------------------------------------------------------------------

file(GLOB SOURCE_FILES src/*.cpp)

# Resoucre files and header files are only added in order to make them available in your IDE.
file(GLOB HEADER_FILES src/*.hpp)
file(GLOB_RECURSE RESOURCE_FILES textures/*)

add_library(csp-vr-accessibility SHARED
  ${SOURCE_FILES}
  ${HEADER_FILES}
  ${RESOURCE_FILES}
)

target_link_libraries(csp-vr-accessibility
  PUBLIC
    cs-core
)

# Add this Plugin to a "plugins" folder in your IDE.
set_property(TARGET csp-vr-accessibility PROPERTY FOLDER "plugins")

# We mark all resource files as "header" in order to make sure that no one tries to compile them.
set_source_files_properties(${RESOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

# Make directory structure available in your IDE.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES 
  ${SOURCE_FILES} ${HEADER_FILES} ${RESOURCE_FILES}
)

# install plugin -----------------------------------------------------------------------------------

install(TARGETS csp-vr-accessibility DESTINATION "share/plugins")
install(DIRECTORY "textures"         DESTINATION "share/resources")
install(DIRECTORY "gui"              DESTINATION "share/resources")
