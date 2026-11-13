/**
 * @file AutomationWindowSteps.cpp
 * @brief 자동화 시나리오의 창 단계를 구현합니다. 자기 창에 OS 창 메시지를 보내고(`PostWindowMessage`), OS 입력 큐에 마우스 이동을 넣고(`SendInput`), OS 커서 클립(`ExpectCursorClip`)을 확인하고,
 *        화면에 합성된 창 내용을 PNG 로 찍습니다(`CaptureWindow`). 전경은 단계가 아니라 시나리오 루트 속성(`requireForeground`)이고 실행기가 이 파일의
 *        `AutomationWindowUtil::bringToForeground` 로 가져온다.
 * @details 사람의 마우스나 포그라운드와 무관하게 같은 창 처리기를 탄다(바깥 스크립트의 `SendInput` 은 포그라운드 창에만 간다). 포커스 메시지(`KillFocus` · `SetFocus`)는
 *          창의 `IWindow::sendFocusEvent` 로 보내 Windows 와 X11 에서 돈다. 나머지는 Windows 에만 구현이 있고, 구현이 없는 플랫폼(포커스도 Wayland 는 없다)에서는 같은 이름의 단계가
 *          시나리오를 건너뜀(13)으로 끝낸다. 이 기계에서 볼 수 없는 것을 초록으로 두지 않기 위해서다.
 *          등록은 이 .cpp 의 정적 등록자가 한다(Shipping 은 Engine 을 통째로 링크한다 — `sw_link_whole_archive`).
 */
#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationWindowSteps.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Foundation/EnginePlatformHeaders.h"
#endif

namespace sw
{
    namespace
    {
        struct AutomationWindowStepsInternal
        {
            static expected<void> validateCapture( const AutomationStep& step )
            {
                AutomationFileStepParams params;
                const expected<void>     read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                if ( StringUtil::endsWith( params._file, ".png", true ) )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "file must end with .png", params._file } );
            }

            static expected<void> validateSendInput( const AutomationStep& step )
            {
                AutomationSendInputParams params;
                const expected<void>      read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                if ( params._dx != 0 || params._dy != 0 )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "dx or dy must be non-zero (a zero move sends no raw report)" } );
            }

            static expected<void> validateAppSuspend( const AutomationStep& step )
            {
                AutomationAppSuspendParams params;
                const expected<void>       read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                if ( params._ticks > 0 )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "ticks must be above 0" } );
            }

            /** @brief 앱 멈춤을 흉내 냅니다. 메인 루프가 `ticks` 틱 동안 프레임을 돌지 않고 돌아온다(플랫폼 수명 이벤트와 같은 길). */
            static bool runAppSuspend( AutomationStepContext& runner, const AutomationStep& step )
            {
                MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr )
                    runner.recordFailure( step, "there is no window to suspend" );
                else
                    pWindow->simulateAppInterruption( step.readValidatedProperties<AutomationAppSuspendParams>()._ticks );
                return true;
            }

            /** @brief 자기 창에 포커스 이벤트를 보냅니다. 창 시스템이 보낼 길이 없으면(Wayland) 시나리오를 건너뜀으로 끝냅니다. */
            static bool runFocusMessage( AutomationStepContext& runner, const AutomationStep& step, bool bFocused )
            {
                MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr )
                    runner.recordFailure( step, "there is no window to send a message to" );
                else if ( pWindow->sendFocusEvent( bFocused ) == false )
                    runner.endScenario( AutomationStepEnd::Skip, step.describe() + ": this window cannot send itself a focus event" );
                return true;
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
                const AutomationWindowMessage message = step.readValidatedProperties<AutomationWindowMessageParams>()._message;
                RECT                          client{};
                GetClientRect( hwnd, &client );
                const LPARAM center = MAKELPARAM( ( client.right - client.left ) / 2, ( client.bottom - client.top ) / 2 );
                // 처리기가 돈 뒤 돌아오도록 보낸다(SendMessage). 창 닫기만 다른 창 메시지처럼 큐로(PostMessage) 보내고, 처리기 안에서 루프를 끝낸다.
                switch ( message )
                {
                    case AutomationWindowMessage::ActivateInactive:
                        SendMessageW( hwnd, WM_ACTIVATE, WA_INACTIVE, 0 );
                        break;
                    case AutomationWindowMessage::ActivateActive:
                        SendMessageW( hwnd, WM_ACTIVATE, WA_ACTIVE, 0 );
                        break;
                    case AutomationWindowMessage::KillFocus:
                        return runFocusMessage( runner, step, false );
                    case AutomationWindowMessage::SetFocus:
                        return runFocusMessage( runner, step, true );
                    case AutomationWindowMessage::LeftButtonDownClient:
                        SendMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, center );
                        break;
                    case AutomationWindowMessage::LeftButtonUpClient:
                        SendMessageW( hwnd, WM_LBUTTONUP, 0, center );
                        break;
                    case AutomationWindowMessage::Close:
                        PostMessageW( hwnd, WM_CLOSE, 0, 0 );
                        break;
                }
                return true;
            }

            /**
             * @brief OS 입력 큐에 상대 마우스 이동을 넣습니다(`SendInput` + `MOUSEEVENTF_MOVE`). 실제 마우스처럼 원시 입력(WM_INPUT)과 커서 이동이 함께 난다.
             * @details `SendInput` 은 전경 창의 입력 큐로만 간다 — 이 창이 전경이 아니거나, 더 높은 무결성 창 · 잠긴 화면(UIPI)이 넣기를 막으면 볼 수 없는 것이라 건너뜀(13)이다.
             */
            static bool runSendInput( AutomationStepContext& runner, const AutomationStep& step )
            {
                const AutomationSendInputParams params = step.readValidatedProperties<AutomationSendInputParams>();
                const HWND                      hwnd   = findWindowHandle();
                if ( hwnd == nullptr || GetForegroundWindow() != hwnd )
                {
                    runner.endScenario( AutomationStepEnd::Skip, step.describe() + ": SendInput reaches only the foreground window and this window is not it" );
                    return true;
                }
                vector<INPUT> listInput( params._count );
                for ( INPUT& input : listInput )
                {
                    input.type       = INPUT_MOUSE;
                    input.mi.dx      = static_cast<LONG>( params._dx );
                    input.mi.dy      = static_cast<LONG>( params._dy );
                    input.mi.dwFlags = MOUSEEVENTF_MOVE;
                }
                const UINT sentCount = SendInput( static_cast<UINT>( listInput.size() ), listInput.data(), sizeof( INPUT ) );
                if ( sentCount != listInput.size() )
                {
                    StringBuilder<constant::buffer::kMax256> reason;
                    reason.appendFormat( "%#: SendInput inserted %# of %# events (error %#) - the input is blocked here (UIPI or a locked desktop)", step.describe().c_str(),
                                         static_cast<uint32>( sentCount ), static_cast<uint32>( listInput.size() ), static_cast<uint32>( GetLastError() ) );
                    runner.endScenario( AutomationStepEnd::Skip, reason.view() );
                }
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
                const bool bWantLocked  = step.readValidatedProperties<AutomationCursorClipParams>()._state == AutomationCursorClipState::Locked;
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
                StringBuilder<constant::buffer::kMax256> failure;
                failure.appendFormat( "cursor clip is not %# (clip %#,%#,%#,%#)", bWantLocked ? "inside the client area" : "the whole screen", static_cast<int32>( clip.left ),
                                      static_cast<int32>( clip.top ), static_cast<int32>( clip.right ), static_cast<int32>( clip.bottom ) );
                runner.recordFailure( step, failure.view() );
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
                    SW_LOG_WARNING_ID( "AutomationWindowSteps.SkippedWindowNotFront", "[Scenario] %# skipped - the window is not in front, so the screen there shows other windows", step.describe().c_str() );
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
                const string path = runner.resolveOutputPath( step.readValidatedProperties<AutomationFileStepParams>()._file );
                if ( FileUtil::ensureParentDirectoryExists( path ) == false ||
                     ImageFileWriter::writePNGRGBA8( path, bytes, static_cast<uint32>( width ), static_cast<uint32>( height ) ) == false )
                {
                    runner.endScenario( AutomationStepEnd::Fail, step.describe() + ": could not write " + path );
                    return true;
                }
                SW_LOG_INFO_ID( "AutomationWindowSteps.WindowCaptureXWindows", "[Scenario] window capture %#x%# (%# windows) -> %#", width, height, static_cast<int32>( context._listRect.size() ), path.c_str() );
                return true;
            }
#elif defined( SW_PLATFORM_POSIX )
            /** @brief 포커스 밖의 창 메시지 · 커서 클립 · 화면 찍기를 볼 구현이 없는 플랫폼입니다. 시나리오를 건너뜀으로 끝냅니다. */
            static bool runUnsupported( AutomationStepContext& runner, const AutomationStep& step )
            {
                runner.endScenario( AutomationStepEnd::Skip, step.describe() + ": this window step is implemented for Windows only" );
                return true;
            }
            static bool runMessage( AutomationStepContext& runner, const AutomationStep& step )
            {
                const AutomationWindowMessage message = step.readValidatedProperties<AutomationWindowMessageParams>()._message;
                if ( message == AutomationWindowMessage::KillFocus || message == AutomationWindowMessage::SetFocus )
                    return runFocusMessage( runner, step, message == AutomationWindowMessage::SetFocus );
                return runUnsupported( runner, step );
            }
            static bool runSendInput( AutomationStepContext& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runClip( AutomationStepContext& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runCapture( AutomationStepContext& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
#else
    #error "Unsupported platform"
#endif
        };
    } // namespace

    bool AutomationWindowUtil::bringToForeground()
    {
#if defined( SW_PLATFORM_WINDOWS )
        const HWND hwnd = AutomationWindowStepsInternal::findWindowHandle();
        return hwnd != nullptr && AutomationWindowStepsInternal::bringToForeground( hwnd );
#elif defined( SW_PLATFORM_POSIX )
        return false;
#else
    #error "Unsupported platform"
#endif
    }

    SW_AUTOMATION_STEP( windowAppSuspend, "SimulateAppSuspend", &AutomationWindowStepsInternal::runAppSuspend, &AutomationWindowStepsInternal::validateAppSuspend, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( windowMessage, "PostWindowMessage", &AutomationWindowStepsInternal::runMessage, &AutomationStep::validateProperties<AutomationWindowMessageParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( windowSendInput, "SendInput", &AutomationWindowStepsInternal::runSendInput, &AutomationWindowStepsInternal::validateSendInput, AutomationStepOptions{}.withDesktop() );
    SW_AUTOMATION_STEP( windowCursorClip, "ExpectCursorClip", &AutomationWindowStepsInternal::runClip, &AutomationStep::validateProperties<AutomationCursorClipParams>, AutomationStepOptions{}.withDesktop() );
    SW_AUTOMATION_STEP( windowCapture, "CaptureWindow", &AutomationWindowStepsInternal::runCapture, &AutomationWindowStepsInternal::validateCapture, AutomationStepOptions{}.withDesktop() );
} // namespace sw
