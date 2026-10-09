#include "pch.h"

#include "App/Module/ModuleCatalogLoader.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleImageUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleCatalog" );

    bool ModuleCatalogLoader::loadAndResolve( uint8 targetMask, ModuleCatalog& outCatalog, ModuleResolution& outResolution )
    {
        // 무엇을 올릴지는 모듈 매니페스트가 정한다 — 빌드가 실행 파일 옆 `Modules/` 에 복사해 둔 것을 CMake 와 같은 규칙으로 해석한다.
        const string catalogDirectory = ModuleImageUtil::getModuleDirectory();
        string       moduleError;
        if ( outCatalog.loadDirectory( catalogDirectory, moduleError ) == false )
        {
            SW_LOG_ERROR( "Module catalog: %#", moduleError.c_str() );
            return false;
        }
        ModuleResolveContext resolveContext{};
        resolveContext._platform      = ModuleCatalog::getCurrentPlatform();
        resolveContext._configuration = ModuleCatalog::getCurrentConfiguration();
        resolveContext._targetMask    = targetMask;
        if ( outCatalog.resolve( resolveContext, outResolution, moduleError ) == false )
        {
            SW_LOG_ERROR( "Module manifests: %#", moduleError.c_str() );
            return false;
        }
        SW_LOG_INFO( "Modules: %# active, %# off", outResolution._listLoadOrder.size(), outResolution._listInactive.size() );
        for ( [[maybe_unused]] const ModuleInactiveEntry& inactive : outResolution._listInactive ) // Shipping 은 로그가 빠진다
            SW_LOG_INFO( "Module %# is off — %#", inactive._name.c_str(), inactive._reason.c_str() );
        return true;
    }
} // namespace sw
