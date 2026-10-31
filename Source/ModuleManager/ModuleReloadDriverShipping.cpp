#include "pch.h"

#include "ModuleManager/ModuleReloadDriver.h"

// 배포 빌드의 구현이다 — 모듈은 정적으로 링크해 매니페스트 · 리로드 매니저 · 개발 명령 `module.reload` · 감시 값 `Module.*` 가 없다.
// 개발 빌드는 `ModuleReloadDriver.cpp` 를 CMake 가 고른다(`Source/ModuleManager/CMakeLists.txt`).
namespace sw
{
    struct ModuleReloadDriver::State
    {
    };

    ModuleReloadDriver::ModuleReloadDriver()
        : _pState{ nullptr }
    {
    }

    ModuleReloadDriver::~ModuleReloadDriver() = default;

    bool ModuleReloadDriver::initialize( [[maybe_unused]] uint8 targetMask, [[maybe_unused]] SubsystemScope& hostScope, [[maybe_unused]] ModuleLoaderSubsystem& moduleLibraryHost,
                                         [[maybe_unused]] ModuleCatalog& outCatalog, [[maybe_unused]] ModuleResolution& outResolution )
    {
        return true;
    }

    Subscription ModuleReloadDriver::addOnBeforeReloadBatch( [[maybe_unused]] BeforeReloadBatchDelegate onBeforeBatch )
    {
        return Subscription{};
    }

    void ModuleReloadDriver::requestReload( [[maybe_unused]] const utf8* pModuleName )
    {
    }

    void ModuleReloadDriver::update()
    {
    }

    LiveReloadManager* ModuleReloadDriver::getLiveReloadManager() const
    {
        return nullptr;
    }

    bool ModuleReloadDriver::queueReload( [[maybe_unused]] const utf8* pModuleName )
    {
        return false;
    }

    bool ModuleReloadDriver::isReloadPending() const
    {
        return false;
    }
} // namespace sw
