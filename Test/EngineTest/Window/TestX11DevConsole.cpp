#include "pch.h"

#include "Engine/Window/DevConsoleWindow.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    #include "Core/Common/X11Headers.h"
#endif

// X11DevConsoleTest — 리눅스 개발 콘솔 오버레이(`X11DevConsoleWindow`)를 진짜 X 창 위에 세워 보고 그려진 픽셀을 읽는다(EditorFeatureGaps Cheat.C2 의 실기 확인을 대신한다).
// 진짜 X 서버가 필요하다(CI 는 xvfb). 한글 · 일본어는 X 서버의 iso10646-1 글꼴이 글자를 가진 만큼만 나오므로 모양은 단언하지 않고, 줄이 그려지고 UTF-8 글이 죽지 않는지를 본다.

SW_TEST_REQUIRES_HOST( X11DevConsoleTest, "creates a real X window; headless CI runners have no display" );

/**
 * @brief [X11DevConsoleTest] 콘솔 오버레이가 서고 줄을 그린다 — 라틴 글 줄에 칠한 픽셀이 있고, 한글 · 일본어가 든 줄을 넘겨도 죽지 않는다
 * @details 변이: `redraw` 가 `XDrawString16` 대신 아무것도 그리지 않으면 첫 줄 띠가 모두 검어 진다. X 서버가 없거나 창을 매핑하지 못하면 건너뛴다.
 */
SW_TEST_CASE( X11DevConsoleTest, OverlayDrawsLatinLineAndSurvivesUtf8Lines )
{
    SW_TEST_REQUIRES_FEATURE( X11ConsoleOverlay ); // 구성에 없음은 의도라 빈 스위트로 지지 않는다
#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );
    if ( window->initializeWindow( "ConsoleOverlayTestWindow", 400, 200 ) == false )
        SW_TEST_SKIP( "no X server to open windows on" );
    window->showWindow( true );
    (void)window->processMessages();

    sw::unique_ptr<sw::IDevConsoleWindow> console = sw::IDevConsoleWindow::createPlatform( *window );
    SW_ASSERT_TRUE_MSG( console != nullptr, "the console overlay could not be created (no iso10646-1 X font?)" );
    console->setVisible( true );
    const sw::vector<sw::string> listLine{ "Console ready", "한글 テスト é" };
    const sw::vector<uint8>      listError{ 0, 0 };
    console->present( listLine, listError );

    Display* pDisplay = static_cast<Display*>( window->getNativeDisplay() );
    SW_ASSERT_NOT_NULL( pDisplay );
    XSync( pDisplay, 0 ); // X11MacroUndef.h 가 매크로 False 를 지운다 — 값 0
    const ::Window owner = static_cast<::Window>( reinterpret_cast<uintptr_t>( window->getNativeHandle() ) );
    ::Window       rootWindow{ 0 };
    ::Window       parentWindow{ 0 };
    ::Window*      pChildren{ nullptr };
    uint32         childCount{ 0 };
    SW_ASSERT_TRUE( XQueryTree( pDisplay, owner, &rootWindow, &parentWindow, &pChildren, &childCount ) != 0 );
    SW_ASSERT_TRUE_MSG( childCount >= 1 && pChildren != nullptr, "the console overlay is not a child window of the game window" );
    const ::Window consoleWindow = pChildren[childCount - 1];
    XFree( pChildren );

    XWindowAttributes attributes{};
    SW_ASSERT_TRUE( XGetWindowAttributes( pDisplay, consoleWindow, &attributes ) != 0 );
    if ( attributes.map_state != IsViewable )
        SW_TEST_SKIP( "the X server did not map the overlay (no usable top-level mapping here)" );
    XImage* pImage = XGetImage( pDisplay, consoleWindow, 0, 0, static_cast<uint32>( attributes.width ), static_cast<uint32>( attributes.height ), AllPlanes, ZPixmap );
    SW_ASSERT_NOT_NULL( pImage );
    // 첫 줄 띠(위 여백 6 에서 줄 높이 16)에 검지 않은 픽셀이 있다.
    uint32 paintedPixelCount = 0;
    for ( int32 y = 6; y < 22 && y < attributes.height; ++y )
    {
        for ( int32 x = 0; x < attributes.width; ++x )
        {
            if ( XGetPixel( pImage, x, y ) != 0 )
                ++paintedPixelCount;
        }
    }
    XDestroyImage( pImage );
    SW_EXPECT_TRUE_MSG( paintedPixelCount > 0, "the first console line left no pixels" );

    console.reset();
    window->destroy();
#endif
}
