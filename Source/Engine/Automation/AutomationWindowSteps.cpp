/**
 * @file AutomationWindowSteps.cpp
 * @brief 자동화 시나리오의 창 단계를 구현합니다. 자기 창에 OS 창 메시지를 보내고(`PostWindowMessage`), OS 커서 클립(`ExpectCursorClip`)과 전경(`RequireForeground`)을 확인하고,
 *        화면에 합성된 창 내용을 PNG 로 찍습니다(`CaptureWindow`).
 * @details 사람의 마우스나 포그라운드와 무관하게 같은 창 처리기를 탄다(바깥 스크립트의 `SendInput` 은 포그라운드 창에만 간다). Windows 에만 구현이 있고,
 *          다른 플랫폼에서는 같은 이름의 단계가 시나리오를 건너뜀(13)으로 끝낸다. 이 기계에서 볼 수 없는 것을 초록으로 두지 않기 위해서다.
 */
#include "pch.h"

#include "Engine/Automation/AutomationWindowSteps.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Resource/Image/ImageFileWriter.h"
#include "Engine/Window/IWindow.h"

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
            static bool checkOnly( const AutomationStep& step, string_view allowed, bool bRequired, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    if ( string_view{ attribute._name } != allowed )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                if ( bRequired && step.findAttribute( allowed ) == nullptr )
                {
                    outError = step.describe() + ": needs " + string( allowed ) + "=\"…\"";
                    return false;
                }
                return true;
            }

            static bool isKnownMessage( string_view message )
            {
                return message == "WM_ACTIVATE_INACTIVE" || message == "WM_ACTIVATE_ACTIVE" || message == "WM_KILLFOCUS" || message == "WM_SETFOCUS" ||
                       message == "WM_CLOSE" || message == "WM_LBUTTONDOWN_CLIENT" || message == "WM_LBUTTONUP_CLIENT";
            }

            static bool validateMessage( const AutomationStep& step, string& outError )
            {
                if ( checkOnly( step, "message", true, outError ) == false )
                    return false;
                if ( isKnownMessage( *step.findAttribute( "message" ) ) )
                    return true;
                outError = step.describe() + ": unknown message '" + *step.findAttribute( "message" ) +
                           "' (WM_ACTIVATE_INACTIVE · WM_ACTIVATE_ACTIVE · WM_KILLFOCUS · WM_SETFOCUS · WM_CLOSE · WM_LBUTTONDOWN_CLIENT · WM_LBUTTONUP_CLIENT)";
                return false;
            }

            static bool validateClip( const AutomationStep& step, string& outError )
            {
                if ( checkOnly( step, "state", true, outError ) == false )
                    return false;
                const string& state = *step.findAttribute( "state" );
                if ( state == "locked" || state == "free" )
                    return true;
                outError = step.describe() + ": state must be locked or free, got '" + state + "'";
                return false;
            }

            static bool validateForeground( const AutomationStep& step, string& outError ) { return checkOnly( step, "", false, outError ); }

            static bool validateCapture( const AutomationStep& step, string& outError )
            {
                if ( checkOnly( step, "file", true, outError ) == false )
                    return false;
                if ( StringUtil::endsWith( *step.findAttribute( "file" ), ".png", true ) )
                    return true;
                outError = step.describe() + ": file must end with .png";
                return false;
            }

#if defined( SW_PLATFORM_WINDOWS )
            static HWND findWindowHandle()
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                return pWindow != nullptr ? static_cast<HWND>( pWindow->getNativeHandle() ) : nullptr;
            }

            /**
             * @brief 창을 전경으로 가져옵니다. 전경 스레드의 입력 큐에 잠깐 붙어 `SetForegroundWindow` 의 전경 잠금을 넘습니다(키 입력을 흉내 내지 않는다).
             * @return 이 호출 뒤 창이 전경이면 true
             * @details 다른 프로세스가 전경이면 `SetForegroundWindow` 만으로는 거절되고 작업 표시줄만 깜박인다(다른 작업자의 App 창이 떠 있는 기계).
             */
            static bool bringToForeground( HWND hwnd )
            {
                if ( GetForegroundWindow() == hwnd )
                    return true;
                if ( IsIconic( hwnd ) != FALSE )
                    ShowWindow( hwnd, SW_RESTORE );
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

            static bool runMessage( AutomationRunner& runner, const AutomationStep& step )
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

            static bool runClip( AutomationRunner& runner, const AutomationStep& step )
            {
                const HWND hwnd = findWindowHandle();
                RECT       clip{};
                RECT       client{};
                if ( hwnd == nullptr || GetClipCursor( &clip ) == FALSE || GetClientRect( hwnd, &client ) == FALSE )
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
                // `RequireForeground` 가 전경을 못 얻을 때와 같이 건너뜀(13)으로 끝낸다.
                const bool bLostForeground = bWantLocked && GetForegroundWindow() != hwnd;
                if ( bLostForeground )
                {
                    runner.finish( AutomationResult::Skipped, step.describe() + ": the window lost the foreground while the scenario ran, so the cursor lock cannot be observed" );
                    return true;
                }
                runner.recordFailure( step, string( "cursor clip is not " ) + ( bWantLocked ? "inside the client area" : "the whole screen" ) + " (clip " +
                                                to_string( static_cast<int32>( clip.left ) ) + "," + to_string( static_cast<int32>( clip.top ) ) + "," +
                                                to_string( static_cast<int32>( clip.right ) ) + "," + to_string( static_cast<int32>( clip.bottom ) ) + ")" );
                return true;
            }

            static bool runForeground( AutomationRunner& runner, const AutomationStep& /*step*/ )
            {
                const HWND hwnd = findWindowHandle();
                if ( hwnd != nullptr )
                    bringToForeground( hwnd );
                if ( hwnd == nullptr || GetForegroundWindow() != hwnd )
                    runner.finish( AutomationResult::Skipped, "the window could not become the foreground window in this session" );
                return true;
            }

            /**
             * @brief 클라이언트 영역을 데스크톱 화면에서 복사해 PNG 로 씁니다. 에디터 UI 까지 포함한 실제 화면입니다(Present 캡처는 UI 를 그리기 전의 주 출력입니다).
             * @details DWM 이 합성한 화면을 읽으므로 네 백엔드 모두 같은 경로다. 주의: 창이 가려져 있으면 **가린 다른 창이 찍힌다**(사용자의 다른 프로그램 화면이
             *          파일로 남는다). 그래서 창이 전경이 아니면 찍지 않고 경고만 남긴다. 화면을 보려는 시나리오는 앞에 `RequireForeground` 를 둔다.
             */
            static bool runCapture( AutomationRunner& runner, const AutomationStep& step )
            {
                const HWND hwnd = findWindowHandle();
                RECT       clientRect{};
                POINT      origin{ 0, 0 };
                if ( hwnd == nullptr || GetClientRect( hwnd, &clientRect ) == FALSE || ClientToScreen( hwnd, &origin ) == FALSE )
                {
                    runner.finish( AutomationResult::Failed, step.describe() + ": no window to capture" );
                    return true;
                }
                if ( GetForegroundWindow() != hwnd || IsIconic( hwnd ) != FALSE )
                {
                    SW_LOG_WARNING( "[Scenario] %# skipped - the window is not in front, so the screen there shows other windows", step.describe().c_str() );
                    return true;
                }
                const int32 width  = clientRect.right - clientRect.left;
                const int32 height = clientRect.bottom - clientRect.top;
                if ( width <= 0 || height <= 0 )
                {
                    runner.finish( AutomationResult::Failed, step.describe() + ": the window has an empty client area" );
                    return true;
                }

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
                    runner.finish( AutomationResult::Failed, step.describe() + ": BitBlt/GetDIBits failed" );
                    return true;
                }
                for ( size_t offset = 0; offset < bytes.size(); offset += 4 )
                {
                    const uint8 blue  = bytes[offset];
                    bytes[offset]     = bytes[offset + 2];
                    bytes[offset + 2] = blue;
                    bytes[offset + 3] = 255;
                }
                const string path = runner.resolveOutputPath( *step.findAttribute( "file" ) );
                FileUtil::ensureParentDirectoryExists( path );
                if ( ImageFileWriter::writePngRgba8( path, bytes, static_cast<uint32>( width ), static_cast<uint32>( height ) ) == false )
                {
                    runner.finish( AutomationResult::Failed, step.describe() + ": could not write " + path );
                    return true;
                }
                SW_LOG_INFO( "[Scenario] window capture %#x%# -> %#", width, height, path.c_str() );
                return true;
            }
#else
            /** @brief 창 메시지와 커서 클립을 볼 구현이 없는 플랫폼입니다. 시나리오를 건너뜀으로 끝냅니다. */
            static bool runUnsupported( AutomationRunner& runner, const AutomationStep& step )
            {
                runner.finish( AutomationResult::Skipped, step.describe() + ": window steps are implemented for Windows only" );
                return true;
            }
            static bool runMessage( AutomationRunner& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runClip( AutomationRunner& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runForeground( AutomationRunner& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
            static bool runCapture( AutomationRunner& runner, const AutomationStep& step ) { return runUnsupported( runner, step ); }
#endif
        };
    } // namespace

    void AutomationWindowSteps::ensureLinked()
    {
    }

    SW_AUTOMATION_STEP( windowMessage, "PostWindowMessage", &AutomationWindowStepsInternal::runMessage, &AutomationWindowStepsInternal::validateMessage, false );
    SW_AUTOMATION_STEP( windowCursorClip, "ExpectCursorClip", &AutomationWindowStepsInternal::runClip, &AutomationWindowStepsInternal::validateClip, false );
    SW_AUTOMATION_STEP( windowForeground, "RequireForeground", &AutomationWindowStepsInternal::runForeground, &AutomationWindowStepsInternal::validateForeground,
                        false );
    SW_AUTOMATION_STEP( windowCapture, "CaptureWindow", &AutomationWindowStepsInternal::runCapture, &AutomationWindowStepsInternal::validateCapture, false );
} // namespace sw
