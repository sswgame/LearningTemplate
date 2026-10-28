/**
 * @file AutomationWindowSteps.cpp
 * @brief 자동화 시나리오의 창 단계를 구현합니다. 자기 창에 OS 창 메시지를 보내고(`PostWindowMessage`), OS 커서 클립(`ExpectCursorClip`)을 확인하고,
 *        화면에 합성된 창 내용을 PNG 로 찍습니다(`CaptureWindow`). 전경은 단계가 아니라 시나리오 루트 속성(`requireForeground`)이고 실행기가 이 파일의
 *        `AutomationWindowUtil::bringToForeground` 로 가져온다.
 * @details 사람의 마우스나 포그라운드와 무관하게 같은 창 처리기를 탄다(바깥 스크립트의 `SendInput` 은 포그라운드 창에만 간다). Windows 에만 구현이 있고,
 *          다른 플랫폼에서는 같은 이름의 단계가 시나리오를 건너뜀(13)으로 끝낸다. 이 기계에서 볼 수 없는 것을 초록으로 두지 않기 위해서다.
 *          등록은 이 .cpp 의 정적 등록자가 한다(Shipping 은 Engine 을 통째로 링크한다 — `sw_linkWholeArchive`).
 */
#include "pch.h"

#include "Engine/Automation/Scenario/AutomationWindowSteps.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/Image/ImageFileWriter.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "AutomationWindowSteps" );

    namespace
    {
        struct AutomationWindowStepsInternal
        {
            static bool isKnownMessage( string_view message )
            {
                return message == "WM_ACTIVATE_INACTIVE" || message == "WM_ACTIVATE_ACTIVE" || message == "WM_KILLFOCUS" || message == "WM_SETFOCUS" ||
                       message == "WM_CLOSE" || message == "WM_LBUTTONDOWN_CLIENT" || message == "WM_LBUTTONUP_CLIENT";
            }

            static bool validateMessage( const AutomationStep& step, string& outError )
            {
                if ( step.hasRequiredAttributes( { "message" }, { "message" }, outError ) == false )
                    return false;
                if ( isKnownMessage( *step.findAttribute( "message" ) ) )
                    return true;
                outError = step.describe() + ": unknown message '" + *step.findAttribute( "message" ) +
                           "' (WM_ACTIVATE_INACTIVE · WM_ACTIVATE_ACTIVE · WM_KILLFOCUS · WM_SETFOCUS · WM_CLOSE · WM_LBUTTONDOWN_CLIENT · WM_LBUTTONUP_CLIENT)";
                return false;
            }

            static bool validateClip( const AutomationStep& step, string& outError )
            {
                if ( step.hasRequiredAttributes( { "state" }, { "state" }, outError ) == false )
                    return false;
                const string& state = *step.findAttribute( "state" );
                if ( state == "locked" || state == "free" )
                    return true;
                outError = step.describe() + ": state must be locked or free, got '" + state + "'";
                return false;
            }

            static bool validateCapture( const AutomationStep& step, string& outError )
            {
                if ( step.hasRequiredAttributes( { "file" }, { "file" }, outError ) == false )
                    return false;
                if ( StringUtil::endsWith( *step.findAttribute( "file" ), ".png", true ) )
                    return true;
                outError = step.describe() + ": file must end with .png";
                return false;
            }

#if defined( SW_PLATFORM_WINDOWS )
            static HWND findWindowHandle()
            {
                MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                return pWindow != nullptr ? static_cast<HWND>( pWindow->tryGetWin32Handle() ) : nullptr;
            }

            /**
             * @brief 창을 전경으로 가져옵니다. 전경 스레드의 입력 큐에 잠깐 붙어 `SetForegroundWindow` 의 전경 잠금을 넘습니다(키 입력을 흉내 내지 않는다).
             * @return 이 호출 뒤 창이 전경이면 true
             * @details 다른 프로세스가 전경이면 `SetForegroundWindow` 만으로는 거절되고 작업 표시줄만 깜박인다(다른 작업자의 App 창이 떠 있는 기계).
             */
            static bool bringToForeground( HWND hwnd )
            {
                // 최소화된 창도 전경(활성)일 수 있다 — 그때는 클라이언트가 없어 커서를 가둘 수 없으므로 먼저 복원한다.
                if ( IsIconic( hwnd ) != FALSE )
                    ShowWindow( hwnd, SW_RESTORE );
                if ( GetForegroundWindow() == hwnd )
                    return true;
                const HWND  foreground       = GetForegroundWindow();
                const DWORD foregroundThread = foreground != nullptr ? GetWindowThreadProcessId( foreground, nullptr ) : 0;
                const DWORD thisThread       = GetCurrentThreadId();
                const bool  bAttach          = foregroundThread != 0 && foregroundThread != thisThread && AttachThreadInput( thisThread, foregroundThread, TRUE ) != FALSE;
                BringWindowToTop( hwnd );
                SetForegroundWindow( hwnd );
                if ( bAttach )
                    AttachThreadInput( thisThread, foregroundThread, FALSE );
                return GetForegroundWindow() == hwnd;
            }

            static bool runMessage( AutomationStepContext& runner, const AutomationStep& step )
            {
                const HWND hwnd = findWindowHandle();
                if ( hwnd == nullptr )
                {
                    runner.recordFailure( step, "there is no window to send a message to" );
                    return true;
                }
                const string& message = *step.findAttribute( "message" );
                RECT          client{};
                GetClientRect( hwnd, &client );
                const LPARAM center = MAKELPARAM( ( client.right - client.left ) / 2, ( client.bottom - client.top ) / 2 );
                // 처리기가 돈 뒤 돌아오도록 보낸다(SendMessage). 창 닫기만 다른 창 메시지처럼 큐로(PostMessage) 보내고, 처리기 안에서 루프를 끝낸다.
                if ( message == "WM_ACTIVATE_INACTIVE" )
                    SendMessageW( hwnd, WM_ACTIVATE, WA_INACTIVE, 0 );
                else if ( message == "WM_ACTIVATE_ACTIVE" )
                    SendMessageW( hwnd, WM_ACTIVATE, WA_ACTIVE, 0 );
                else if ( message == "WM_KILLFOCUS" )
                    SendMessageW( hwnd, WM_KILLFOCUS, 0, 0 );
                else if ( message == "WM_SETFOCUS" )
                    SendMessageW( hwnd, WM_SETFOCUS, 0, 0 );
                else if ( message == "WM_LBUTTONDOWN_CLIENT" )
                    SendMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, center );
                else if ( message == "WM_LBUTTONUP_CLIENT" )
                    SendMessageW( hwnd, WM_LBUTTONUP, 0, center );
                else
                    PostMessageW( hwnd, WM_CLOSE, 0, 0 );
                return true;
            }

            static bool runClip( AutomationStepContext& runner, const AutomationStep& step )
            {
                const HWND hwnd = findWindowHandle();
                RECT       clip{};
                RECT       client{};
                const bool bCursorClipUnreadable = hwnd == nullptr || GetClipCursor( &clip ) == FALSE || GetClientRect( hwnd, &client ) == FALSE;
                if ( bCursorClipUnreadable )
                {
                    runner.recordFailure( step, "could not read the cursor clip or the window rectangle" );
                    return true;
                }
                POINT topLeft{ client.left, client.top };
                POINT bottomRight{ client.right, client.bottom };
                ClientToScreen( hwnd, &topLeft );
                ClientToScreen( hwnd, &bottomRight );
                const bool bInsideClient = clip.left >= topLeft.x && clip.top >= topLeft.y && clip.right <= bottomRight.x && clip.bottom <= bottomRight.y;
                const RECT screen{ GetSystemMetrics( SM_XVIRTUALSCREEN ), GetSystemMetrics( SM_YVIRTUALSCREEN ),
                                   GetSystemMetrics( SM_XVIRTUALSCREEN ) + GetSystemMetrics( SM_CXVIRTUALSCREEN ),
                                   GetSystemMetrics( SM_YVIRTUALSCREEN ) + GetSystemMetrics( SM_CYVIRTUALSCREEN ) };
                const bool bWholeScreen = clip.left <= screen.left && clip.top <= screen.top && clip.right >= screen.right && clip.bottom >= screen.bottom;
                const bool bWantLocked  = *step.findAttribute( "state" ) == "locked";
                if ( bWantLocked ? bInsideClient : bWholeScreen )
                    return true;
                // 커서는 전경 창만 가둔다(`InputManager::applyMouseLockMode`). 도는 사이 다른 창이 전경을 가져갔으면 잠금은 볼 수 없는 것이지 틀린 것이 아니다 —
                // 시작할 때 전경을 못 얻을 때(`requireForeground`)와 같이 건너뜀(13)으로 끝낸다.
                const bool bLostForeground = bWantLocked && GetForegroundWindow() != hwnd;
                if ( bLostForeground )
                {
                    runner.endScenario( AutomationStepEnd::Skip, step.describe() + ": the window lost the foreground while the scenario ran, so the cursor lock cannot be observed" );
                    return true;
                }
                runner.recordFailure( step, string( "cursor clip is not " ) + ( bWantLocked ? "inside the client area" : "the whole screen" ) + " (clip " +
                                                to_string( static_cast<int32>( clip.left ) ) + "," + to_string( static_cast<int32>( clip.top ) ) + "," +
                                                to_string( static_cast<int32>( clip.right ) ) + "," + to_string( static_cast<int32>( clip.bottom ) ) + ")" );
                return true;
            }

            /** @brief `EnumWindows` 로 같은 프로세스의 보이는 최상위 창(뷰포트 옵션 팝업 같은 플랫폼 창)의 화면 사각형을 모으는 문맥입니다. */
            struct PopupCollectContext
            {
                HWND         _mainWindow{ nullptr };
                DWORD        _processID{ 0 };
                vector<RECT> _listRect;
            };

            static BOOL CALLBACK collectPopupWindow( HWND hwnd, LPARAM param )
            {
                PopupCollectContext& context = *reinterpret_cast<PopupCollectContext*>( param );
                if ( hwnd == context._mainWindow || IsWindowVisible( hwnd ) == FALSE || IsIconic( hwnd ) != FALSE )
                    return TRUE;
                DWORD processID = 0;
                GetWindowThreadProcessId( hwnd, &processID );
                RECT rect{};
                if ( processID != context._processID || GetWindowRect( hwnd, &rect ) == FALSE || rect.right <= rect.left || rect.bottom <= rect.top )
                    return TRUE;
                context._listRect.push_back( rect );
                return TRUE;
            }

            /**
             * @brief 클라이언트 영역을 데스크톱 화면에서 복사해 PNG 로 씁니다. 에디터 UI 까지 포함한 실제 화면입니다(Present 캡처는 UI 를 그리기 전의 주 출력입니다).
             * @details 뷰포트 옵션 팝업은 `ViewportsEnable` 때문에 별도 OS 창이라 클라이언트 사각형만 찍으면 잘린다 — 같은 프로세스의 보이는 최상위 창(팝업)의 사각형을 클라이언트와 합친
             *          경계 상자를 찍고, 어느 창에도 속하지 않는 픽셀(창 사이로 보이는 다른 프로그램 화면)은 투명(알파 0)으로 지운다. 상용 엔진의 에디터 스크린샷도 팝업 창을 따로 찍는다.
             *          DWM 이 합성한 화면을 읽으므로 네 백엔드 모두 같은 경로다. 주의: 창이 가려져 있으면 **가린 다른 창이 찍힌다**(사용자의 다른 프로그램 화면이
             *          파일로 남는다). 그래서 창이 전경이 아니면 찍지 않고 경고만 남긴다. 화면을 보려는 시나리오는 루트에 `requireForeground="true"` 를 둔다.
             */
            static bool runCapture( AutomationStepContext& runner, const AutomationStep& step )
            {
                const HWND hwnd = findWindowHandle();
                RECT       clientRect{};
                POINT      origin{ 0, 0 };
                const bool bCaptureAreaUnavailable = hwnd == nullptr || GetClientRect( hwnd, &clientRect ) == FALSE || ClientToScreen( hwnd, &origin ) == FALSE;
                if ( bCaptureAreaUnavailable )
                {
                    runner.endScenario( AutomationStepEnd::Fail, step.describe() + ": no window to capture" );
                    return true;
                }
                // 팝업을 눌러 팝업이 전경일 수 있다 — 전경 창이 같은 프로세스의 것이면 우리 화면이다.
                DWORD foregroundProcess = 0;
                if ( const HWND foreground = GetForegroundWindow(); foreground != nullptr )
                    GetWindowThreadProcessId( foreground, &foregroundProcess );
                if ( foregroundProcess != GetCurrentProcessId() || IsIconic( hwnd ) != FALSE )
                {
                    SW_LOG_WARNING( "[Scenario] %# skipped - the window is not in front, so the screen there shows other windows", step.describe().c_str() );
                    return true;
                }
                const int32 clientWidth  = clientRect.right - clientRect.left;
                const int32 clientHeight = clientRect.bottom - clientRect.top;
                if ( clientWidth <= 0 || clientHeight <= 0 )
                {
                    runner.endScenario( AutomationStepEnd::Fail, step.describe() + ": the window has an empty client area" );
                    return true;
                }
                // 캡처 영역 = 클라이언트 ∪ 팝업 창들. 화면 좌표의 사각형 목록과 그 경계 상자를 만든다.
                PopupCollectContext context;
                context._mainWindow = hwnd;
                context._processID  = GetCurrentProcessId();
                context._listRect.push_back( RECT{ origin.x, origin.y, origin.x + clientWidth, origin.y + clientHeight } );
                EnumWindows( &AutomationWindowStepsInternal::collectPopupWindow, reinterpret_cast<LPARAM>( &context ) );
                RECT bounds = context._listRect.front();
                for ( const RECT& rect : context._listRect )
                {
                    bounds.left   = MathUtil::min( bounds.left, rect.left );
                    bounds.top    = MathUtil::min( bounds.top, rect.top );
                    bounds.right  = MathUtil::max( bounds.right, rect.right );
                    bounds.bottom = MathUtil::max( bounds.bottom, rect.bottom );
                }
                origin             = POINT{ bounds.left, bounds.top };
                const int32 width  = bounds.right - bounds.left;
                const int32 height = bounds.bottom - bounds.top;

                const HDC     screenDC = GetDC( nullptr );
                const HDC     memoryDC = CreateCompatibleDC( screenDC );
                const HBITMAP bitmap   = CreateCompatibleBitmap( screenDC, width, height );
                const HGDIOBJ previous = SelectObject( memoryDC, bitmap );
                const BOOL    bCopied  = BitBlt( memoryDC, 0, 0, width, height, screenDC, origin.x, origin.y, SRCCOPY | CAPTUREBLT );

                BITMAPINFO info{};
                info.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
                info.bmiHeader.biWidth       = width;
                info.bmiHeader.biHeight      = -height; // 위에서 아래 방향이다
                info.bmiHeader.biPlanes      = 1;
                info.bmiHeader.biBitCount    = 32;
                info.bmiHeader.biCompression = BI_RGB;
                vector<uint8> bytes( static_cast<size_t>( width ) * static_cast<size_t>( height ) * 4u );
                const int32   lineCount = GetDIBits( memoryDC, bitmap, 0, static_cast<UINT>( height ), bytes.data(), &info, DIB_RGB_COLORS );

                SelectObject( memoryDC, previous );
                DeleteObject( bitmap );
                DeleteDC( memoryDC );
                ReleaseDC( nullptr, screenDC );

                if ( bCopied == FALSE || lineCount != height )
                {
                    runner.endScenario( AutomationStepEnd::Fail, step.describe() + ": BitBlt/GetDIBits failed" );
                    return true;
                }
                for ( size_t offset = 0; offset < bytes.size(); offset += 4 )
                {
                    const uint8 blue  = bytes[offset];
                    bytes[offset]     = bytes[offset + 2];
                    bytes[offset + 2] = blue;
                    bytes[offset + 3] = 255;
                }
                // 어느 창에도 속하지 않는 픽셀은 다른 프로그램 화면이 찍혔을 수 있어 지운다(알파 0, 색 0).
                for ( int32 y = 0; y < height; ++y )
                {
                    for ( int32 x = 0; x < width; ++x )
                    {
                        const int32 screenX = bounds.left + x;
                        const int32 screenY = bounds.top + y;
                        bool        bInside = false;
                        for ( const RECT& rect : context._listRect )
                        {
                            if ( screenX >= rect.left && screenX < rect.right && screenY >= rect.top && screenY < rect.bottom )
                            {
                                bInside = true;
                                break;
                            }
                        }
                        if ( bInside == false )
                            memset( &bytes[( static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x ) ) * 4u], 0, 4u );
                    }
                }
                const string path = runner.resolveOutputPath( *step.findAttribute( "file" ) );
                if ( FileUtil::ensureParentDirectoryExists( path ) == false ||
                     ImageFileWriter::writePngRgba8( path, bytes, static_cast<uint32>( width ), static_cast<uint32>( height ) ) == false )
                {
                    runner.endScenario( AutomationStepEnd::Fail, step.describe() + ": could not write " + path );
                    return true;
                }
                SW_LOG_INFO( "[Scenario] window capture %#x%# (%# windows) -> %#", width, height, static_cast<int32>( context._listRect.size() ), path.c_str() );
                return true;
            }
#else
            /** @brief 창 메시지와 커서 클립을 볼 구현이 없는 플랫폼입니다. 시나리오를 건너뜀으로 끝냅니다. */
            static bool runUnsupported( AutomationStepContext& runner, const AutomationStep& step )
            {
                runner.endScenario( AutomationStepEnd::Skip, step.describe() + ": window steps are implemented for Windows only" );
                return true;
            }
            static bool runMessage( AutomationStepContext& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runClip( AutomationStepContext& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runCapture( AutomationStepContext& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
#endif
        };
    } // namespace

    bool AutomationWindowUtil::bringToForeground()
    {
#if defined( SW_PLATFORM_WINDOWS )
        const HWND hwnd = AutomationWindowStepsInternal::findWindowHandle();
        return hwnd != nullptr && AutomationWindowStepsInternal::bringToForeground( hwnd );
#else
        return false;
#endif
    }

    SW_AUTOMATION_STEP( windowMessage, "PostWindowMessage", &AutomationWindowStepsInternal::runMessage, &AutomationWindowStepsInternal::validateMessage, false );
    SW_AUTOMATION_STEP_DESKTOP( windowCursorClip, "ExpectCursorClip", &AutomationWindowStepsInternal::runClip, &AutomationWindowStepsInternal::validateClip, false );
    SW_AUTOMATION_STEP_DESKTOP( windowCapture, "CaptureWindow", &AutomationWindowStepsInternal::runCapture, &AutomationWindowStepsInternal::validateCapture, false );
} // namespace sw
