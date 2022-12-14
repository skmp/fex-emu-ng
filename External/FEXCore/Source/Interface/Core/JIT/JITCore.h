#pragma once

#include <memory>

namespace FEXCore::Context {
struct Context;
}

namespace FEXCore::Core {
struct InternalThreadState;
}

namespace FEXCore::CPU {
class CPUBackend;

[[nodiscard]] std::unique_ptr<CPUBackend> CreateX86JITCore(FEXCore::Context::Context *ctx,
                                                           FEXCore::Core::InternalThreadState *Thread);
void InitializeX86JITSignalHandlers(FEXCore::Context::Context *CTX);
CPUBackendFeatures GetX86JITBackendFeatures();

[[nodiscard]] std::unique_ptr<CPUBackend> CreateArm64JITCore(FEXCore::Context::Context *ctx,
                                                             FEXCore::Core::InternalThreadState *Thread);
void InitializeArm64JITSignalHandlers(FEXCore::Context::Context *CTX);
CPUBackendFeatures GetArm64JITBackendFeatures();

} // namespace FEXCore::CPU
