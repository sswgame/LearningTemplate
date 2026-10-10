#include "pch.h"

#include "Engine/Renderer/Capture/RenderDocCapture.h"

#include "Engine/Console/DevCommandRegistry.h"

#if !defined( SW_SHIPPING )
    #include <renderdoc_app.h>
#endif

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#elif defined( SW_PLATFORM_LINUX )
    #include <dlfcn.h>
#endif

namespace sw
{
#if !defined( SW_SHIPPING )
    SW_LOG_CALLER( "RenderDoc" );

    namespace
    {
        struct RenderDocCaptureInternal
        {
            /** @brief 캡처 파일 경로 틀입니다(RenderDoc 이 `_frame<N>.rdc` 를 붙인다). 작업 폴더 기준. */
            static constexpr const utf8* kCaptureFilePathTemplate = "Saved/RenderDoc/capture";

            inline static RENDERDOC_API_1_6_0* _s_pAPI   = nullptr;
            inline static bool                 _s_bTried = false;

            /** @brief 이미 올라온(또는 @p bLoadIfMissing 이면 설치 경로에서 올린) RenderDoc 의 `RENDERDOC_GetAPI` 입니다. 없으면 nullptr. */
            static pRENDERDOC_GetAPI findGetAPI( bool bLoadIfMissing )
            {
    #if defined( SW_PLATFORM_WINDOWS )
                HMODULE hModule = GetModuleHandleW( L"renderdoc.dll" );
                if ( hModule == nullptr && bLoadIfMissing )
                {
                    utf16       arrPath[constant::kMaxPathSize]{};
                    const DWORD length = ExpandEnvironmentStringsW( L"%ProgramFiles%\\RenderDoc\\renderdoc.dll", arrPath, DWORD{ constant::kMaxPathSize } );
                    if ( 0 < length && length <= DWORD{ constant::kMaxPathSize } )
                        hModule = LoadLibraryW( arrPath );
                }
                if ( hModule == nullptr )
                    return nullptr;
                return reinterpret_cast<pRENDERDOC_GetAPI>( reinterpret_cast<void*>( GetProcAddress( hModule, "RENDERDOC_GetAPI" ) ) );
    #elif defined( SW_PLATFORM_LINUX )
                void* pModule = dlopen( "librenderdoc.so", RTLD_NOW | RTLD_NOLOAD );
                if ( pModule == nullptr && bLoadIfMissing )
                    pModule = dlopen( "librenderdoc.so", RTLD_NOW );
                if ( pModule == nullptr )
                    return nullptr;
                return reinterpret_cast<pRENDERDOC_GetAPI>( dlsym( pModule, "RENDERDOC_GetAPI" ) );
    #else
                (void)bLoadIfMissing;
                return nullptr;
    #endif
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
#endif
} // namespace sw

namespace sw
{
#if !defined( SW_SHIPPING )
    void RenderDocCapture::initialize( bool bLoadIfMissing )
    {
        if ( RenderDocCaptureInternal::_s_bTried )
            return;
        RenderDocCaptureInternal::_s_bTried = true;

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
        RenderDocCaptureInternal::_s_pAPI->SetCaptureFilePathTemplate( RenderDocCaptureInternal::kCaptureFilePathTemplate );
        // 화면 글자를 끈다 — 캡처하지 않는 프레임의 스크린샷 · 픽셀 시험이 흔들리지 않게.
        RenderDocCaptureInternal::_s_pAPI->MaskOverlayBits( eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None );
        SW_LOG_INFO( "RenderDoc attached - captures go to %#_frame<N>.rdc", RenderDocCaptureInternal::kCaptureFilePathTemplate );
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
#else
    void RenderDocCapture::initialize( bool bLoadIfMissing )
    {
        (void)bLoadIfMissing;
    }

    bool RenderDocCapture::isAvailable()
    {
        return false;
    }

    void RenderDocCapture::triggerCapture()
    {
    }

    bool RenderDocCapture::launchReplayUI()
    {
        return false;
    }
#endif
} // namespace sw
