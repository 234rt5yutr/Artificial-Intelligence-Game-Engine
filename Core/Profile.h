#pragma once

#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>
    #define PROFILE_FUNCTION() ZoneScoped
    #define PROFILE_SCOPE(name) ZoneScopedN(name)
    #define PROFILE_FRAME() FrameMark
    #define PROFILE_THREAD(name) tracy::SetThreadName(name)
#else
    #define PROFILE_FUNCTION()
    #define PROFILE_SCOPE(name)
    #define PROFILE_FRAME()
    #define PROFILE_THREAD(name)
#endif
