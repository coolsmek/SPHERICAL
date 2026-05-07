#include <iostream>
#include <cstring>
#include "SPHERICAL.h"

// Minimal test to verify the Spherical library initializes correctly
int main() {
    std::cout << "=== Spherical Init Test ===" << std::endl;
    
    // Test 1: Verify Init rejects null parameters
    {
        Spherical::SphericalInitInfo info = {};
        std::memset(&info, 0, sizeof(info));
        
        bool result = Spherical::Init(info);
        if (!result) {
            std::cout << "✓ Init correctly rejected null parameters" << std::endl;
        } else {
            std::cout << "✗ Init should reject null parameters" << std::endl;
            return 1;
        }
    }
    
    // Test 2: Verify Shutdown is safe to call
    {
        Spherical::Shutdown();
        std::cout << "✓ Shutdown executed safely" << std::endl;
    }
    
    // Test 3: Verify NewFrame is safe to call without Init
    {
        Spherical::NewFrame();
        std::cout << "✓ NewFrame executed safely without Init" << std::endl;
    }
    
    std::cout << "\n=== All basic tests passed ===" << std::endl;
    return 0;
}
