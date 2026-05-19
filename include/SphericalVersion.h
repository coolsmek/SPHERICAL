#ifndef SPHERICAL_VERSION_H
#define SPHERICAL_VERSION_H

#define SPHERICAL_VERSION_MAJOR 0
#define SPHERICAL_VERSION_MINOR 2
#define SPHERICAL_VERSION_PATCH 2
#define SPHERICAL_VERSION_STRING "0.2.2-alpha"

namespace Spherical {
    constexpr const char* GetVersionString() {
        return SPHERICAL_VERSION_STRING;
    }
    
    constexpr int GetVersionMajor() {
        return SPHERICAL_VERSION_MAJOR;
    }
    
    constexpr int GetVersionMinor() {
        return SPHERICAL_VERSION_MINOR;
    }
    
    constexpr int GetVersionPatch() {
        return SPHERICAL_VERSION_PATCH;
    }
}

#endif // SPHERICAL_VERSION_H

