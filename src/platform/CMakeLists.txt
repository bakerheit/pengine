# platform -> apricot_host. The window, the GL context, and the translation of
# raw device events into core::InputFrame.
target_sources(apricot_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input.cpp
)

# Keeping an unattended run out of the desktop's foreground is the one thing
# here that no cross-platform library covers -- see foreground.h for why two
# SDL hints look like they do and do not. On Apple that needs Cocoa, so it
# needs Objective-C++; everywhere else it is a no-op and the stub keeps the
# call site free of platform ifdefs.
if(APPLE)
    target_sources(apricot_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/foreground_mac.mm)
    # ARC, so this file never has to think about retain/release. It is ADDED to
    # the target's flags, not a replacement for them: the same -Werror strict
    # set still applies here as everywhere else.
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/foreground_mac.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(apricot_host PRIVATE "-framework AppKit")
else()
    target_sources(apricot_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/foreground_stub.cpp)
endif()
