#include "pch.h"

#include "Core/Module/ModuleImageUtil.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleUnloadListener.h"

SW_LOG_CALLER( "ModuleImageUtil" );
namespace sw
{
    uint32 ModuleImageUtil::releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped )
    {
        if ( pOutKeepImageMapped != nullptr )
            *pOutKeepImageMapped = false;

        // 모듈은 자기가 단 것을 스스로 떼야 한다(에디터는 ImGuiEditor::shutdown · ~ConsolePanel 에서 뗀다). 여기서 뗀 것이 있으면
        // 그 정리가 빠졌다는 뜻이라 경고로 남긴다. 늘 0 이어야 한다.
        vector<IModuleUnloadListener::ReleaseResult> listResult;
        IModuleUnloadListener::releaseAllWithin( pBegin, pEnd, listResult );

        uint32 releasedCount{ 0 };
        for ( const IModuleUnloadListener::ReleaseResult& result : listResult )
        {
            releasedCount += result._releasedCount;
            if ( result._releasedCount > 0 )
                SW_LOG_WARNING( "Module %# left %# %# behind — released them before unloading its image", moduleName, result._releasedCount, result._pListenerName );
            // 떼어 낼 수 없는 것(다른 코드가 아직 구독하는 이 이미지의 이벤트 채널)이 남았다. 내리면 다음 발행 · 종료 때 내려간 코드로 뛴다.
            if ( result._bKeepImageMapped )
            {
                SW_LOG_WARNING( "Module %# is still referenced by %# that other code uses — keeping its image mapped until exit", moduleName, result._pListenerName );
                if ( pOutKeepImageMapped != nullptr )
                    *pOutKeepImageMapped = true;
            }
        }
        return releasedCount;
    }

    bool ModuleImageUtil::unloadModuleImage( string_view moduleName, void* pHandle )
    {
        if ( pHandle == nullptr )
            return false;

        const void* pBegin{ nullptr };
        const void* pEnd{ nullptr };
        bool        bKeepImageMapped{ false };
        if ( FileUtil::findDynamicLibraryRange( pHandle, pBegin, pEnd ) )
            (void)releaseModuleCode( moduleName, pBegin, pEnd, &bKeepImageMapped ); // 뗀 것은 releaseModuleCode 가 경고로 남긴다
        if ( bKeepImageMapped )
            return false;

        // 이 이미지가 끌어온 의존 이미지가 함께 내려가지 않게 한다. 그 이미지의 코드를 쥔 등록은 여기서 뗄 수 없다.
        (void)FileUtil::pinDynamicLibraryDependencies( pHandle );
        FileUtil::unloadDynamicLibrary( pHandle );
        return true;
    }
} // namespace sw
