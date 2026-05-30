#pragma once

#ifdef WIN32
    #ifdef CVARSYSTEM_API_EXPORT
        #define CVARSYSTEM_API __declspec(dllexport)
    #else
        #define CVARSYSTEM_API __declspec(dllimport)
    #endif
#else
    #define CVARSYSTEM_API
#endif