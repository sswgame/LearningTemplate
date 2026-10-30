#include "pch.h"

#include "DevTools/RenderDoc/RenderDocCapture.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/ProcessEnvironment.h"

#include <renderdoc_app.h>

namespace sw
{
    SW_LOG_CALLER( "RenderDoc" );

    namespace
    {
        struct RenderDocCaptureInternal
        {
            /** @brief 캡처 파일 경로 템플릿입니다(RenderDoc 이 `_frame<N>.rdc` 를 붙입니다). 실행 산출물 루트(`ProcessRoots::_saved`) 아래다. */
            static string makeCaptureFilePathTemplate() { return FileUtil::joinPath( ProcessRoots::get()._saved.str(), "RenderDoc/capture" ); }

            inline static RENDERDOC_API_1_6_0* _s_pAPI = nullptr;
            inline static atomic<bool>         _s_bTried{ false };

            /** @brief 설치된 RenderDoc 라이브러리를 로드합니다. Windows 는 설치 폴더(`%ProgramFiles%/RenderDoc`), Linux 는 동적 링커 검색 경로입니다. 없으면 nullptr 입니다. */
            static void* loadInstalledLibrary( const string& libraryName )
            {
#if defined( SW_PLATFORM_WINDOWS )
                string programFiles;
                if ( ProcessEnvironment::tryReadNonEmpty( "ProgramFiles", programFiles ) == false )
                    return nullptr;
                const std::optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( FileUtil::joinPath( FileUtil::joinPath( programFiles, "RenderDoc" ), libraryName ) );
                if ( libraryPath.has_value() == false || FileUtil::isRegularFile( libraryPath->str() ) == false )
                    return nullptr; // 설치하지 않았다 - 로드 실패 경고를 내지 않는다
                return ModuleLibraryUtil::loadLibraryAt( *libraryPath );
#else
                return ModuleLibraryUtil::loadSystemLibrary( libraryName );
#endif
            }

            /** @brief 이미 로드된(또는 @p bLoadIfMissing 이면 설치 경로에서 로드한) RenderDoc 의 `RENDERDOC_GetAPI` 입니다. 없으면 nullptr 입니다. */
            static pRENDERDOC_GetAPI findGetAPI( bool bLoadIfMissing )
            {
                // 주입된 RenderDoc 은 참조 없이 빌리지 않는다 - 고정해 API 포인터(`_s_pAPI`)가 사는 동안 내려가지 않게 한다.
                const string libraryName = ModuleLibraryUtil::formatSharedLibraryName( "renderdoc" );
                void*        pModule     = ModuleLibraryUtil::findLoadedLibrary( libraryName );
                if ( pModule == nullptr && bLoadIfMissing )
                    pModule = loadInstalledLibrary( libraryName );
                return reinterpret_cast<pRENDERDOC_GetAPI>( ModuleLibraryUtil::getDynamicSymbol( pModule, "RENDERDOC_GetAPI" ) ); // 모듈이 없으면 nullptr
            }

            static bool runCapture( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                if ( RenderDocCapture::isAvailable() == false )
                {
                    outReply = "RenderDoc is not attached - start with -renderdoc or launch from RenderDoc";
                    return false;
                }
                RenderDocCapture::triggerCapture();
                outReply = "capturing the next frame";
                return true;
            }

            static bool runUI( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                if ( RenderDocCapture::launchReplayUI() == false )
                {
                    outReply = "RenderDoc replay UI could not be started";
                    return false;
                }
                outReply = "RenderDoc replay UI started";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( RenderDocCaptureFrame, "renderdoc.capture", "renderdoc.capture", "Capture the next frame with RenderDoc (needs -renderdoc or a RenderDoc launch)",
                    &RenderDocCaptureInternal::runCapture );
    SW_DEV_COMMAND( RenderDocReplayUI, "renderdoc.ui", "renderdoc.ui", "Open the RenderDoc replay UI attached to this process", &RenderDocCaptureInternal::runUI );
} // namespace sw

namespace sw
{
    void RenderDocCapture::initialize( bool bLoadIfMissing )
    {
        if ( RenderDocCaptureInternal::_s_bTried.exchange( true ) ) // 첫 호출 하나만 초기화한다
            return;

        const pRENDERDOC_GetAPI pfnGetAPI = RenderDocCaptureInternal::findGetAPI( bLoadIfMissing );
        if ( pfnGetAPI == nullptr )
        {
            if ( bLoadIfMissing )
                SW_LOG_WARNING( "-renderdoc: RenderDoc is not installed in the default location - captures are unavailable" );
            return;
        }
        void* pAPI = nullptr;
        if ( pfnGetAPI( eRENDERDOC_API_Version_1_6_0, &pAPI ) != 1 || pAPI == nullptr )
        {
            SW_LOG_WARNING( "RenderDoc is loaded but did not provide API 1.6.0 - captures are unavailable" );
            return;
        }
        RenderDocCaptureInternal::_s_pAPI = static_cast<RENDERDOC_API_1_6_0*>( pAPI );
        const string captureTemplate      = RenderDocCaptureInternal::makeCaptureFilePathTemplate();
        RenderDocCaptureInternal::_s_pAPI->SetCaptureFilePathTemplate( captureTemplate.c_str() );
        // 화면 글자를 끈다. 캡처하지 않는 프레임의 스크린샷과 픽셀 테스트가 흔들리지 않게 하기 위해서다.
        RenderDocCaptureInternal::_s_pAPI->MaskOverlayBits( eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None );
        SW_LOG_INFO( "RenderDoc attached - captures go to %#_frame<N>.rdc", captureTemplate );
    }

    bool RenderDocCapture::isAvailable()
    {
        return RenderDocCaptureInternal::_s_pAPI != nullptr;
    }

    void RenderDocCapture::triggerCapture()
    {
        if ( RenderDocCaptureInternal::_s_pAPI == nullptr )
            return;
        RenderDocCaptureInternal::_s_pAPI->TriggerCapture();
        SW_LOG_INFO( "RenderDoc capture requested for the next frame" );
    }

    bool RenderDocCapture::launchReplayUI()
    {
        if ( RenderDocCaptureInternal::_s_pAPI == nullptr )
            return false;
        if ( RenderDocCaptureInternal::_s_pAPI->IsTargetControlConnected() != 0 )
            return RenderDocCaptureInternal::_s_pAPI->ShowReplayUI() != 0;
        return RenderDocCaptureInternal::_s_pAPI->LaunchReplayUI( 1, nullptr ) != 0;
    }
} // namespace sw
