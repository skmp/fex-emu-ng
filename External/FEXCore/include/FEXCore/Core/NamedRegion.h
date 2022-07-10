#pragma once
#include <memory>
#include <string>


namespace FEXCore::HLE {
    struct SourcecodeMap;
}

namespace FEXCore::IR {
    class AOTIRCache;
}

namespace FEXCore::Core {

struct NamedRegion {
    std::string FileId;
    std::string Filename;
    std::string Fingerprint;
    
    std::unique_ptr<FEXCore::IR::AOTIRCache> AOTIRCache;
    std::unique_ptr<FEXCore::HLE::SourcecodeMap> SourcecodeMap;
    
    bool ContainsCode;
};

}