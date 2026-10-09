#include "pch.h"

#include "Server/Windows/WindowsServiceHost.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Process/ShutdownSignal.h"

    #include "Server/ServerApp.h"

namespace sw
{
    namespace
    {
        struct WindowsServiceHostInternal
        {
            /** @brief SCM 은 SERVICE_WIN32_OWN_PROCESS 에서 이 이름을 보지 않는다 — 등록 이름은 `sc create` 가 정한다. */
            static constexpr const utf16* kServiceName = L"SwServer";
            /** @brief 정지 요청 뒤 SCM 에 알리는 대기 힌트(밀리초) — 종료 유예(기본 10 초)보다 넉넉하게. */
            static constexpr DWORD kStopWaitHintMilliseconds  = 15000;
            static constexpr DWORD kStartWaitHintMilliseconds = 30000;

            static inline SERVICE_STATUS_HANDLE s_hStatus{ nullptr };
            static inline SERVICE_STATUS        s_status{};
            static inline int32                 s_argc{ 0 };
            static inline utf8**                s_ppArgv{ nullptr };
            static inline int32                 s_exitCode{ 0 };

            static void reportStatus( DWORD state, DWORD waitHint )
            {
                s_status.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
                s_status.dwCurrentState            = state;
                s_status.dwControlsAccepted        = state == SERVICE_RUNNING ? ( SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN ) : 0;
                s_status.dwWin32ExitCode           = s_exitCode == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
                s_status.dwServiceSpecificExitCode = static_cast<DWORD>( s_exitCode );
                s_status.dwWaitHint                = waitHint;
                ++s_status.dwCheckPoint;
                (void)SetServiceStatus( s_hStatus, &s_status );
            }

            static DWORD WINAPI onControl( DWORD control, DWORD, LPVOID, LPVOID )
            {
                if ( control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_PRESHUTDOWN || control == SERVICE_CONTROL_SHUTDOWN )
                {
                    reportStatus( SERVICE_STOP_PENDING, kStopWaitHintMilliseconds );
                    ShutdownSignal::request( ShutdownCause::ServiceStop );
                    return NO_ERROR;
                }
                return control == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
            }

            static void WINAPI serviceMain( DWORD, LPWSTR* )
            {
                s_hStatus = RegisterServiceCtrlHandlerExW( kServiceName, &onControl, nullptr );
                if ( s_hStatus == nullptr )
                    return;
                ShutdownSignal::setServiceMode( true );
                reportStatus( SERVICE_START_PENDING, kStartWaitHintMilliseconds );
                // 기동 완료를 알릴 훅이 본문에 없으니 본문 앞에서 RUNNING 을 보고한다(기동 실패는 STOPPED + 종료 코드로 드러난다).
                reportStatus( SERVICE_RUNNING, 0 );
                s_exitCode = ServerApp::runMain( s_argc, s_ppArgv );
                reportStatus( SERVICE_STOPPED, 0 );
            }

            /** @brief 작업 폴더를 실행 파일 폴더로 옮깁니다. */
            static void enterExecutableDirectory()
            {
                utf16       arrPath[MAX_PATH]{};
                const DWORD length = GetModuleFileNameW( nullptr, arrPath, MAX_PATH );
                if ( length == 0 || length >= MAX_PATH )
                    return;
                for ( DWORD index = length; index > 0; --index )
                {
                    if ( arrPath[index - 1] == L'\\' || arrPath[index - 1] == L'/' )
                    {
                        arrPath[index - 1] = L'\0';
                        break;
                    }
                }
                (void)SetCurrentDirectoryW( arrPath );
            }
        };
    } // namespace

    int32 WindowsServiceHost::runIfRequested( int32 argc, utf8* pArgv[] )
    {
        bool bService = false;
        for ( int32 index = 1; index < argc; ++index )
        {
            bService = bService || string_view( pArgv[index] ) == "--service";
        }
        if ( bService == false )
            return -1;
        WindowsServiceHostInternal::enterExecutableDirectory();
        WindowsServiceHostInternal::s_argc   = argc;
        WindowsServiceHostInternal::s_ppArgv = pArgv;
        SERVICE_TABLE_ENTRYW arrEntry[]      = {
            {const_cast<LPWSTR>( WindowsServiceHostInternal::kServiceName ), &WindowsServiceHostInternal::serviceMain},
            {                                                       nullptr,                                  nullptr}
        };
        if ( StartServiceCtrlDispatcherW( arrEntry ) == FALSE )
            return 2; // SCM 이 띄운 것이 아니다(콘솔에서 --service) — ERROR_FAILED_SERVICE_CONTROLLER_CONNECT
        return WindowsServiceHostInternal::s_exitCode;
    }
} // namespace sw

#endif
