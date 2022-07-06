#pragma once
#include <memory>
#include <string>


namespace FEXCore::HLE {
    class SourcecodeMap;
}

namespace FEXCore::IR {
    class AOTIRCacheEntry;
}

namespace FEXCore::Core {

struct NamedRegion {
    std::string FileId;
    std::string Filename;
    
    std::unique_ptr<FEXCore::IR::AOTIRCacheEntry> AOTIRCacheEntry;
    std::unique_ptr<FEXCore::HLE::SourcecodeMap> SourcecodeMap;
    
    bool ContainsCode;
};

}