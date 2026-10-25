#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "TestFramework/TestEnvironment.h"
#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

// 플랫폼 창 — 실제 OS 창을 띄우고 리사이즈 콜백과 메시지 훅을 본다.
//
// 진짜 창을 만든다. 헤드리스 CI 러너엔 디스플레이가 없다.

SW_TEST_REQUIRES_HOST( WindowTest, "creates a real OS window; headless CI runners have no display" );

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Window/Windows/Win32Window.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Engine/Window/Linux/X11Window.h"
#endif

// ------------------------------------------------------------------------------
// 1) WindowTest — 수명·리사이즈
// ------------------------------------------------------------------------------
/**
 * @brief [WindowTest] 플랫폼 팩토리와 수명
 */
SW_TEST_CASE( WindowTest, PlatformFactoryAndLifecycle )
{
    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );

    // DPI/디스플레이 스케일에서 클라이언트 크기는 달라질 수 있다.
    // 픽셀 일치 대신 create + 사용 가능한 0이 아닌 크기 + 정상 destroy 를 검증한다.
    constexpr uint32 kReqW = 640;
    constexpr uint32 kReqH = 600;
    SW_EXPECT_TRUE( window->initializeWindow( "LifecycleTestWindow", kReqW, kReqH ) );

    SW_EXPECT_TRUE( window->getWidth() > 0 );
    SW_EXPECT_TRUE( window->getHeight() > 0 );
    window->destroy();
}

/**
 * @brief [WindowTest] 리사이즈 콜백과 커스텀 메시지 핸들러
 */
SW_TEST_CASE( WindowTest, ResizeCallbackAndCustomMessageHandler )
{
    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_EXPECT_TRUE( window != nullptr );

    if ( window != nullptr )
    {
        bool   bResized{ false };
        uint32 newWidth{ 0 };
        uint32 newHeight{ 0 };

        window->setResizeCallback( SW_DELEGATE_LAMBDA( sw::WindowResizeDelegate, [&bResized, &newWidth, &newHeight]( uint32 w, uint32 h )
        {
            bResized  = true;
            newWidth  = w;
            newHeight = h;
        } ) );

        bool bMsgHandled{ false };
        window->setCustomMessageHandler( SW_DELEGATE_LAMBDA( sw::WindowMessageHandlerDelegate, [&bMsgHandled]( const sw::NativeWindowEvent& ) -> bool
        {
            bMsgHandled = true;
            return false;
        } ) );

        bool bCreated = window->initializeWindow( "CallbackTestWindow", 800, 600 );
        SW_EXPECT_TRUE( bCreated );

        window->destroy();
    }
}

/**
 * @brief [WindowTest] 주 창은 엔진 범위의 서브시스템이 소유한다 — 넘긴 창을 범위에서 찾고, 두 번째 창은 거절하며, 내릴 때 파괴한다
 * @details 예전에는 정적 활성 창 포인터가 소유권 전달 칸이자 관찰 포인터였다(창이 죽으면 소멸자가 전역을 끊어야 했다). 이제 소유자가 하나(`MainWindowSubsystem`)라
 *          죽은 포인터를 읽을 길이 없다 — 읽는 쪽은 범위에서 찾고, 서브시스템이 내려가면 창은 없다.
 */
SW_TEST_CASE( WindowTest, MainWindowSubsystemOwnsTheWindow )
{
    test::ScopedEngineServiceOverride scope;
    sw::MainWindowSubsystem&          windowHost = scope.add( sw::make_unique<sw::MainWindowSubsystem>() );
    (void)scope.bind();
    SW_EXPECT_TRUE( windowHost.getWindow() == nullptr );

    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );
    SW_ASSERT_TRUE( window->initializeWindow( "MainWindowOwnerTest", 320, 240 ) );
    sw::IWindow* const pRaw = window.get();
    SW_ASSERT_TRUE( windowHost.adopt( window ) );
    SW_EXPECT_TRUE( window == nullptr ); // 소유권이 넘어갔다
    SW_EXPECT_EQUAL( pRaw, windowHost.getWindow() );
    SW_EXPECT_TRUE( sw::engine::findSubsystem<sw::MainWindowSubsystem>() == &windowHost );

    sw::unique_ptr<sw::IWindow> second = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( second != nullptr );
    SW_EXPECT_FALSE( windowHost.adopt( second ) ); // 주 창을 조용히 바꿔 끼우지 않는다
    SW_EXPECT_TRUE( second != nullptr );           // 거절하면 부르는 쪽에 그대로 남는다
    SW_EXPECT_EQUAL( pRaw, windowHost.getWindow() );

    windowHost.shutdownSubsystem();
    SW_EXPECT_TRUE( windowHost.getWindow() == nullptr );
}

/**
 * @brief [WindowTest] 다시 만들어도 **보이던 창은 보인다** — 그리고 크기·제목을 지킨다
 * @details 다시 만드는 절차는 기반 `IWindow::recreate()` 한 벌이고, 플랫폼은 훅만 구현한다. 절차가 플랫폼마다 따로 있으면
 *          표시 상태를 복원하지 않는 쪽이 생기고, 그 길로 들어온 창은 백엔드 교체(`RHI::recreateDevice` 가 `recreate()` 를
 *          부른다) 뒤 **화면에서 사라진다.**
 *
 *          "보이던 창이었나" 는 `isVisible()` 로 물으면 안 된다 — X11 에서 그것은 "창 관리자가 이미 매핑했는가" 라서,
 *          방금 보이라고 한 창도, 최소화된 창도, 다른 워크스페이스에 있는 창도 거짓이다. 그 상태로 다시 만들면 **창이 사라진다.**
 *          윈도우에서는 `IsWindowVisible` 이 WS_VISIBLE 스타일이라 둘이 같아 윈도우만 보면 초록이다.
 *          엔진은 **의도**(`isVisibleRequested()`)로 판단하고, 이 케이스도 그것을 단언한다.
 */
SW_TEST_CASE( WindowTest, RecreateKeepsVisibilityAndSize )
{
    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );

    constexpr uint32 kReqW = 480;
    constexpr uint32 kReqH = 320;
    SW_ASSERT_TRUE( window->initializeWindow( "RecreateTestWindow", kReqW, kReqH ) );
    window->showWindow( true );

    // **화면 상태를 동기로 답하는 플랫폼인지 여기서 재 둔다.** 윈도우의 `IsWindowVisible` 은
    // `ShowWindow` 가 세운 WS_VISIBLE 스타일이라 즉시 참이지만, X11 의 `IsViewable` 은 **창 관리자가
    // 실제로 매핑한 뒤**에야 참이다 — 방금 보이라고 한 창도 아직 거짓이고, 이 테스트는 이벤트 루프를
    // 돌리지 않는다. 그런 플랫폼에서 화면 상태를 단언하면 **엔진이 아니라
    // 창 관리자를 테스트하는 것**이 된다.
    const bool bPlatformAnswersVisibilitySynchronously = window->isVisible();

    const uint32 widthBefore  = window->getWidth();
    const uint32 heightBefore = window->getHeight();
    SW_ASSERT_TRUE( widthBefore > 0 && heightBefore > 0 );

    SW_ASSERT_TRUE_MSG( window->recreate(), "Failed to recreate the window" );

    // 엔진의 계약은 **의도**다 — "보이기로 한 창은 다시 만든 뒤에도 보이기로 한 상태다".
    // 이것은 모든 플랫폼에서 즉시 답할 수 있고, `recreate()` 가 판단에 쓰는 값도 이것이다.
    SW_EXPECT_TRUE_MSG( window->isVisibleRequested(),
                        "다시 만든 창이 숨김으로 남았습니다 — 백엔드를 바꾸면 화면이 사라집니다" );

    if ( bPlatformAnswersVisibilitySynchronously )
    {
        SW_EXPECT_TRUE_MSG( window->isVisible(),
                            "다시 만든 창이 화면에 없습니다 — 플랫폼 훅이 불리지 않았습니다" );
    }

    SW_EXPECT_EQUAL( widthBefore, window->getWidth() );
    SW_EXPECT_EQUAL( heightBefore, window->getHeight() );

    window->destroy();
}

/**
 * @brief [WindowTest] 최소화해도 창이 기억하는 크기가 0 이 되지 않는다
 * @details 최소화는 클라이언트 영역 `0x0` 짜리 `WM_SIZE` 로 온다. 그 값을 그대로 `_width`/`_height`
 *          에 적으면 "창이 0 칸이다" 가 되어 버린다 — 그리고 `recreate()`(백엔드 교체가 이 길로
 *          온다)는 바로 그 값으로 창을 다시 만든다. 최소화 상태에서 백엔드를 바꾸면 복원한 뒤에도
 *          창이 0 칸짜리로 남는다. 최소화가 말하는 것은 "안 보인다" 지 "0 칸이다" 가 아니다.
 * @note 최소화를 요청하는 길이 플랫폼마다 달라 여기서는 Win32 만 본다. X11 은 아이코니파이가
 *       `ConfigureNotify` 를 0 으로 주지 않으므로 같은 함정이 없다.
 */
SW_TEST_CASE( WindowTest, MinimizingDoesNotForgetTheWindowSize )
{
#if defined( SW_PLATFORM_WINDOWS )
    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );

    constexpr uint32 kReqW = 480;
    constexpr uint32 kReqH = 320;
    SW_ASSERT_TRUE( window->initializeWindow( "MinimizeTestWindow", kReqW, kReqH ) );
    window->showWindow( true );
    window->processMessages();

    const uint32 widthBefore  = window->getWidth();
    const uint32 heightBefore = window->getHeight();
    SW_ASSERT_TRUE( widthBefore > 0 && heightBefore > 0 );

    HWND hWnd = static_cast<HWND>( window->tryGetWin32Handle() );
    SW_ASSERT_TRUE( hWnd != nullptr );

    ShowWindow( hWnd, SW_MINIMIZE );
    window->processMessages();

    SW_EXPECT_TRUE_MSG( window->getWidth() == widthBefore && window->getHeight() == heightBefore,
                        "최소화가 창 크기를 0 으로 덮었습니다 — 이 상태로 recreate 하면 0칸 창이 됩니다" );

    // 그리고 이 상태에서 다시 만들어도 크기를 지켜야 한다 — 백엔드 교체가 정확히 이 길이다.
    if ( window->recreate() )
    {
        SW_EXPECT_EQUAL( widthBefore, window->getWidth() );
        SW_EXPECT_EQUAL( heightBefore, window->getHeight() );
    }

    window->destroy();
#else
    SW_TEST_REQUIRES_FEATURE( WindowsPlatform ); // 최소화 요청 경로가 플랫폼마다 달라 Win32 에서만 봅니다
#endif
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [WindowTest] 리사이즈 콜백 안에서 다시 들어온 WM_SIZE 의 크기를 잃지 않는다 — 마지막 크기로 끝난다
 * @details 재진입한 WM_SIZE 는 크기만 적고 콜백을 건너뛴다. 바깥 호출이 자기가 받은 크기로 끝나면 스왑체인이 다음 WM_SIZE 까지 옛 크기로
 *          남는다(DPI 변경의 `SetWindowPos` 가 콜백 안에서 그 재진입을 만든다).
 */
SW_TEST_CASE( WindowTest, NestedResizeEndsAtTheLastSize )
{
    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );
    SW_ASSERT_TRUE( window->initializeWindow( "NestedResizeTestWindow", 320, 240 ) );
    const HWND hWnd = static_cast<HWND>( window->tryGetWin32Handle() );
    SW_ASSERT_TRUE( hWnd != nullptr );

    sw::vector<sw::pair<uint32, uint32>> listResize;
    window->setResizeCallback( SW_DELEGATE_LAMBDA( sw::WindowResizeDelegate, [&listResize, hWnd]( uint32 width, uint32 height )
    {
        listResize.push_back( { width, height } );
        if ( listResize.size() == 1 )
            SendMessageW( hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM( 640, 480 ) ); // 콜백 안에서 크기가 또 바뀐다
    } ) );

    SendMessageW( hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM( 800, 600 ) );
    SW_ASSERT_TRUE( listResize.empty() == false );
    SW_EXPECT_EQUAL( 640u, listResize.back().first );
    SW_EXPECT_EQUAL( 480u, listResize.back().second );
    SW_EXPECT_EQUAL( size_t( 2 ), listResize.size() );

    window->setResizeCallback( sw::WindowResizeDelegate{} );
    window->destroy();
}

/**
 * @brief [WindowTest] 잠금 중 포커스를 잃었다 돌아와도 제목 표시줄 · X 자리에 커서가 갈 수 있다
 * @details 실제 창 · 실제 `ClipCursor` 로 본다. 제목 표시줄 · X 를 눌러 창을 활성화하면 `WM_ACTIVATE` 가 그 클릭보다 먼저 온다 —
 *          그때 다시 잠그면 커서가 클라이언트 안으로 끌려 들어가 창을 끌 수 없다. 창이 전경을 못 얻으면(다른 창이 쥐고 있다)
 *          잠금이 걸리지 않으니 "잠금 중엔 갇힌다" 판정만 건너뛴다.
 */
SW_TEST_CASE( WindowTest, MouseLockLeavesTheCloseButtonReachable )
{
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []()
    {
        ClipCursor( nullptr );
    } ) );

    test::ScopedEngineServiceOverride scope;
    sw::MainWindowSubsystem&          windowHost = scope.add( sw::make_unique<sw::MainWindowSubsystem>() );
    (void)scope.bind();

    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    window->setCustomMessageHandler( SW_DELEGATE_LAMBDA( sw::WindowMessageHandlerDelegate, [&input]( const sw::NativeWindowEvent& event ) -> bool
    {
        input.processNativeEvent( event );
        return false;
    } ) );
    SW_ASSERT_TRUE( window->initializeWindow( "MouseLockCloseButtonWindow", 640, 360 ) );
    sw::IWindow* const pWindow = window.get();
    SW_ASSERT_TRUE( windowHost.adopt( window ) ); // 입력 관리자가 범위에서 주 창을 찾는다
    pWindow->showWindow( true );

    const HWND hWnd = static_cast<HWND>( pWindow->tryGetWin32Handle() );
    SW_ASSERT_TRUE( hWnd != nullptr );
    SetForegroundWindow( hWnd );
    (void)pWindow->processMessages();

    RECT windowRect{};
    GetWindowRect( hWnd, &windowRect );
    const POINT closeButton{ windowRect.right - 12, windowRect.top + 8 };
    const auto  isReachable = [&closeButton]() -> bool
    {
        RECT clipRect{};
        GetClipCursor( &clipRect );
        return PtInRect( &clipRect, closeButton ) != FALSE;
    };

    input.setMouseLockMode( sw::MouseLockMode::LockedInCenter );
    if ( GetForegroundWindow() == hWnd )
        SW_EXPECT_FALSE( isReachable() ); // 잠금 중엔 클라이언트 안에 갇힌다

    SendMessageW( hWnd, WM_ACTIVATE, WA_INACTIVE, 0 );
    SW_EXPECT_TRUE( isReachable() );
    SendMessageW( hWnd, WM_ACTIVATE, WA_CLICKACTIVE, 0 ); // 제목 표시줄을 눌러 활성화했다
    SW_EXPECT_TRUE_MSG( isReachable(), "활성화만으로 커서를 다시 가둬 X 에 닿을 수 없습니다" );

    input.setMouseLockMode( sw::MouseLockMode::None );
    input.shutdown();
    pWindow->setCustomMessageHandler( sw::WindowMessageHandlerDelegate{} );
    pWindow->destroy();
}

/**
 * @brief [WindowTest] 창 제목이 OS 에 UTF-16 그대로 들어간다 — 만들 때도, `SetWindowTextW` 로 바꿀 때도
 * @details 창 클래스 · 창을 W 판으로 만들고 창 프로시저가 A 판 기본 처리(`DefWindowProc` — 이 저장소는 UNICODE 를 정의하지 않는다)로 끝나면
 *          `WM_NCCREATE` · `WM_SETTEXT` 의 UTF-16 제목을 ANSI 로 읽어 첫 글자에서 끊는다. 제목 줄 · 작업 표시줄에 "S" 한 글자만 남는다.
 */
SW_TEST_CASE( WindowTest, TitleReachesTheOsAsUtf16 )
{
    constexpr const utf8* kTitle   = "SWEngine Title Test - 창 제목";
    constexpr const utf8* kRenamed = "Renamed Title - 바뀐 제목";

    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );
    SW_ASSERT_TRUE( window->initializeWindow( kTitle, 320, 240 ) );
    const HWND hWnd = static_cast<HWND>( window->tryGetWin32Handle() );
    SW_ASSERT_TRUE( hWnd != nullptr );

    const auto readTitle = [hWnd]() -> sw::string
    {
        utf16       arrText[sw::constant::kMaxBuffer256]{};
        const int32 length               = GetWindowTextW( hWnd, arrText, static_cast<int32>( sw::constant::kMaxBuffer256 ) );
        arrText[length < 0 ? 0 : length] = L'\0';
        return sw::StringUtil::utf16ToUtf8( arrText );
    };
    SW_EXPECT_EQUAL( sw::string( kTitle ), readTitle() );

    const sw::wstring renamed = sw::StringUtil::utf8ToUtf16( kRenamed );
    SetWindowTextW( hWnd, renamed.c_str() );
    SW_EXPECT_EQUAL( sw::string( kRenamed ), readTitle() );

    window->destroy();
}
#endif

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
namespace
{
    /** @brief 클립보드 주인 창의 사건을 계속 받아 다른 창(다른 프로그램 역할)의 `SelectionRequest` 에 답하는 스레드입니다. 주인 창의 디스플레이 연결은 이 스레드만 쓴다. */
    class X11ClipboardOwnerPump
    {
    public:
        explicit X11ClipboardOwnerPump( sw::X11Window& owner )
            : _owner{ owner }
            , _bStop{ false }
            , _worker{ [this]()
        {
            while ( _bStop.load() == false )
            {
                (void)_owner.processMessages();
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        } }
        {
        }

        ~X11ClipboardOwnerPump()
        {
            _bStop.store( true );
            _worker.join();
        }

        X11ClipboardOwnerPump( const X11ClipboardOwnerPump& )            = delete;
        X11ClipboardOwnerPump& operator=( const X11ClipboardOwnerPump& ) = delete;

    private:
        sw::X11Window&   _owner;
        sw::atomic<bool> _bStop;
        sw::thread       _worker;
    };

    /** @brief @p owner 가 CLIPBOARD 를 쥐고 @p reader 가 읽은 글이 @p text 와 같은지 봅니다(둘은 서로 다른 디스플레이 연결이라 다른 프로그램처럼 서로 요청한다). */
    void expectClipboardRoundTrip( sw::X11Window& owner, sw::X11Window& reader, const sw::string& text, const utf8* pLabel )
    {
        SW_ASSERT_TRUE_MSG( owner.writeClipboardText( text ), pLabel );
        sw::string read;
        {
            X11ClipboardOwnerPump pump( owner ); // 주인이 SelectionRequest(큰 글이면 INCR 조각의 PropertyNotify)에 답하는 동안 읽는다
            SW_EXPECT_TRUE_MSG( reader.readClipboardText( read ), pLabel );
        }
        SW_EXPECT_TRUE_MSG( read == text, pLabel );
    }
} // namespace
#endif

/**
 * @brief [WindowTest] 리눅스 클립보드: 한 창이 복사한 글(한글 포함)을 다른 창이 읽고, 반대로도 되며, X 요청 한 번보다 큰 글(INCR)도 두 방향으로 온전히 간다
 * @details 두 창은 각자 디스플레이 연결을 가져 서로 다른 프로그램처럼 요청한다(`XConvertSelection` → `SelectionNotify`). UiInputRemaining I3 의 "다른 프로그램과 주고받기" 를 xvfb 위에서 본다.
 *          변이: `X11Window::beginClipboardIncr` 를 건너뛰면 큰 글이 요청 한계에서 잘리거나 거절되어 진다. X 서버가 없으면 건너뛰고, 리눅스 클라이언트가 아니어도 건너뛴다.
 */
SW_TEST_CASE( WindowTest, X11ClipboardRoundTripsBetweenWindowsIncludingIncr )
{
#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
    sw::unique_ptr<sw::IWindow> firstBase  = sw::IWindow::createPlatformWindow();
    sw::unique_ptr<sw::IWindow> secondBase = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( firstBase != nullptr && secondBase != nullptr );
    if ( firstBase->initializeWindow( "ClipboardFirst", 200, 100 ) == false || secondBase->initializeWindow( "ClipboardSecond", 200, 100 ) == false )
        SW_TEST_SKIP( "no X server to open windows on" );
    sw::X11Window* pFirst  = sw::X11Window::tryCast( firstBase.get() );
    sw::X11Window* pSecond = sw::X11Window::tryCast( secondBase.get() );
    if ( pFirst == nullptr || pSecond == nullptr )
        SW_TEST_SKIP( "the platform window is not an X11 window (the clipboard is X11 only)" );
    sw::X11Window& first  = *pFirst;
    sw::X11Window& second = *pSecond;

    const sw::string small = "복사 → paste 한글 日本語 end";
    expectClipboardRoundTrip( first, second, small, "small text first -> second" );
    expectClipboardRoundTrip( second, first, small, "small text second -> first" );

    // X 요청 한 번(BIG-REQUESTS 포함)보다 큰 글 — INCR 로 가야 한다. 마지막 조각 경계의 글자가 깨지지 않게 여러 바이트 글자를 섞는다.
    sw::string big;
    big.reserve( 40u * 1024u * 1024u );
    while ( big.size() < 40u * 1024u * 1024u )
    {
        big += "가나다abc라마바\n";
    }
    expectClipboardRoundTrip( first, second, big, "INCR first -> second" );
    expectClipboardRoundTrip( second, first, big, "INCR second -> first" );

    firstBase->destroy();
    secondBase->destroy();
#else
    SW_TEST_REQUIRES_FEATURE( LinuxPlatform );
    SW_TEST_REQUIRES_FEATURE( ClientCode ); // the X11 clipboard exists only in Linux client builds
#endif
}
