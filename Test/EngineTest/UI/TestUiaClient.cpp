#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/Window/IWindow.h"

#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/UI/Accessibility/Windows/UiaAccessibilityBridge.h"

    #include <objbase.h> // WIN32_LEAN_AND_MEAN 이면 `interface` 매크로가 없다 — UIAutomationClient.h 가 쓴다
    #include <oleauto.h>
    #include <UIAutomationClient.h>
#endif

// UiaClientTest — 내레이터 실기 확인(ScreenReaderSupport A6)을 대신한다. 진짜 창에 UIA 브리지를 달고, 같은 프로세스의 UI Automation 클라이언트(내레이터가 쓰는 같은 COM 길)가
// 다른 스레드에서 메뉴를 읽고(이름 · 컨트롤 타입) 누르고(Invoke) 슬라이더 값을 바꾸고(RangeValue) 포커스 이동 알림을 받는지 본다. 진짜 창이 필요해 CI 러너는 돌지 않는다.

SW_TEST_REQUIRES_HOST( UiaClientTest, "creates a real window and drives it with a UI Automation client; headless CI runners have no desktop" );

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
namespace
{
    /** @brief 클라이언트 스레드가 읽어 온 값입니다(메인 스레드가 합류 뒤에 판정한다). */
    struct UiaClientObservation
    {
        bool       _bConnected{ false };
        bool       _bButtonFound{ false };
        int32      _buttonControlType{ 0 };
        bool       _bInvoked{ false };
        bool       _bSliderFound{ false };
        int32      _sliderControlType{ 0 };
        float64    _sliderValueBefore{ -1.0 };
        float64    _sliderMaximum{ -1.0 };
        bool       _bSliderSet{ false };
        bool       _bCheckFound{ false };
        int32      _checkToggleState{ -1 };
        bool       _bDisabledSeen{ false };
        bool       _bDisabledEnabledProperty{ true };
        sw::string _error{};
    };

    /** @brief 포커스가 옮겨 갔다는 UIA 알림을 받아 그 요소의 이름을 적는 처리기입니다(알림을 듣는 클라이언트가 있어야 브리지가 일한다). */
    class UiaFocusRecorder final : public IUIAutomationFocusChangedEventHandler
    {
    public:
        UiaFocusRecorder() = default;

        HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppObject ) override
        {
            if ( ppObject == nullptr )
                return E_POINTER;
            if ( riid == __uuidof( IUnknown ) || riid == __uuidof( IUIAutomationFocusChangedEventHandler ) )
            {
                *ppObject = static_cast<IUIAutomationFocusChangedEventHandler*>( this );
                AddRef();
                return S_OK;
            }
            *ppObject = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>( InterlockedIncrement( &_refCount ) ); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const LONG count = InterlockedDecrement( &_refCount );
            if ( count == 0 )
                sw_delete( this );
            return static_cast<ULONG>( count );
        }
        HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent( IUIAutomationElement* pSender ) override
        {
            BSTR       bstrName   = nullptr;
            const bool bHasSender = pSender != nullptr && SUCCEEDED( pSender->get_CurrentName( &bstrName ) );
            if ( bHasSender && bstrName != nullptr )
            {
                std::lock_guard<sw::mutex> lock( _mutex );
                _lastName = sw::StringUtil::utf16ToUtf8( bstrName );
                SysFreeString( bstrName );
            }
            return S_OK;
        }

        sw::string getLastName() const
        {
            std::lock_guard<sw::mutex> lock( _mutex );
            return _lastName;
        }

    private:
        ~UiaFocusRecorder() = default;

        LONG              _refCount{ 1 };
        mutable sw::mutex _mutex;
        sw::string        _lastName;
    };

    /** @brief 이름이 @p pName 인 자손 요소를 찾습니다. 못 찾으면 nullptr 입니다(호출한 쪽이 Release). */
    IUIAutomationElement* findByName( IUIAutomation* pAutomation, IUIAutomationElement* pRoot, const utf16* pName )
    {
        VARIANT value;
        VariantInit( &value );
        value.vt                           = VT_BSTR;
        value.bstrVal                      = SysAllocString( pName );
        IUIAutomationCondition* pCondition = nullptr;
        const HRESULT           created    = pAutomation->CreatePropertyCondition( UIA_NamePropertyId, value, &pCondition );
        VariantClear( &value );
        if ( FAILED( created ) || pCondition == nullptr )
            return nullptr;
        IUIAutomationElement* pFound = nullptr;
        (void)pRoot->FindFirst( TreeScope_Descendants, pCondition, &pFound );
        pCondition->Release();
        return pFound;
    }

    /** @brief 클라이언트 스레드 본문입니다. 메인 스레드가 창 메시지를 돌리는 동안 UIA 로 메뉴를 읽고 누르고 바꿉니다. */
    void runUiaClient( HWND hwnd, UiaFocusRecorder* pRecorder, UiaClientObservation* pOut )
    {
        const HRESULT comInit = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        if ( FAILED( comInit ) )
        {
            pOut->_error = "CoInitializeEx failed";
            return;
        }
        IUIAutomation* pAutomation = nullptr;
        if ( FAILED( CoCreateInstance( __uuidof( CUIAutomation ), nullptr, CLSCTX_INPROC_SERVER, __uuidof( IUIAutomation ), reinterpret_cast<void**>( &pAutomation ) ) ) ||
             pAutomation == nullptr )
        {
            pOut->_error = "CUIAutomation could not be created";
            CoUninitialize();
            return;
        }
        // 알림을 듣는 클라이언트가 있어야 브리지가 포커스를 알린다(내레이터도 듣는다).
        (void)pAutomation->AddFocusChangedEventHandler( nullptr, pRecorder );

        IUIAutomationElement* pRoot = nullptr;
        if ( SUCCEEDED( pAutomation->ElementFromHandle( hwnd, &pRoot ) ) && pRoot != nullptr )
        {
            pOut->_bConnected = true;

            if ( IUIAutomationElement* pButton = findByName( pAutomation, pRoot, L"Play" ) )
            {
                pOut->_bButtonFound = true;
                CONTROLTYPEID type  = 0;
                if ( SUCCEEDED( pButton->get_CurrentControlType( &type ) ) )
                    pOut->_buttonControlType = static_cast<int32>( type );
                IUnknown* pPattern = nullptr;
                if ( SUCCEEDED( pButton->GetCurrentPattern( UIA_InvokePatternId, &pPattern ) ) && pPattern != nullptr )
                {
                    IUIAutomationInvokePattern* pInvoke = nullptr;
                    if ( SUCCEEDED( pPattern->QueryInterface( __uuidof( IUIAutomationInvokePattern ), reinterpret_cast<void**>( &pInvoke ) ) ) && pInvoke != nullptr )
                    {
                        pOut->_bInvoked = SUCCEEDED( pInvoke->Invoke() );
                        pInvoke->Release();
                    }
                    pPattern->Release();
                }
                pButton->Release();
            }

            if ( IUIAutomationElement* pSlider = findByName( pAutomation, pRoot, L"Volume" ) )
            {
                pOut->_bSliderFound = true;
                CONTROLTYPEID type  = 0;
                if ( SUCCEEDED( pSlider->get_CurrentControlType( &type ) ) )
                    pOut->_sliderControlType = static_cast<int32>( type );
                IUnknown* pPattern = nullptr;
                if ( SUCCEEDED( pSlider->GetCurrentPattern( UIA_RangeValuePatternId, &pPattern ) ) && pPattern != nullptr )
                {
                    IUIAutomationRangeValuePattern* pRange = nullptr;
                    if ( SUCCEEDED( pPattern->QueryInterface( __uuidof( IUIAutomationRangeValuePattern ), reinterpret_cast<void**>( &pRange ) ) ) && pRange != nullptr )
                    {
                        (void)pRange->get_CurrentValue( &pOut->_sliderValueBefore );
                        (void)pRange->get_CurrentMaximum( &pOut->_sliderMaximum );
                        pOut->_bSliderSet = SUCCEEDED( pRange->SetValue( 7.0 ) );
                        pRange->Release();
                    }
                    pPattern->Release();
                }
                pSlider->Release();
            }

            if ( IUIAutomationElement* pCheck = findByName( pAutomation, pRoot, L"Mute" ) )
            {
                pOut->_bCheckFound = true;
                IUnknown* pPattern = nullptr;
                if ( SUCCEEDED( pCheck->GetCurrentPattern( UIA_TogglePatternId, &pPattern ) ) && pPattern != nullptr )
                {
                    IUIAutomationTogglePattern* pToggle = nullptr;
                    if ( SUCCEEDED( pPattern->QueryInterface( __uuidof( IUIAutomationTogglePattern ), reinterpret_cast<void**>( &pToggle ) ) ) && pToggle != nullptr )
                    {
                        ToggleState state = ToggleState_Off;
                        if ( SUCCEEDED( pToggle->get_CurrentToggleState( &state ) ) )
                            pOut->_checkToggleState = static_cast<int32>( state );
                        pToggle->Release();
                    }
                    pPattern->Release();
                }
                pCheck->Release();
            }

            if ( IUIAutomationElement* pQuit = findByName( pAutomation, pRoot, L"Quit" ) )
            {
                pOut->_bDisabledSeen = true;
                BOOL bEnabled        = TRUE;
                if ( SUCCEEDED( pQuit->get_CurrentIsEnabled( &bEnabled ) ) )
                    pOut->_bDisabledEnabledProperty = bEnabled != FALSE;
                pQuit->Release();
            }
            pRoot->Release();
        }
        else
        {
            pOut->_error = "ElementFromHandle failed";
        }
        // 포커스 알림은 메인 스레드가 돌고 있는 동안 도착해야 하므로 처리기는 호출한 쪽이 기다린 뒤 푼다(스레드가 끝나기 전에 푼다).
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        (void)pAutomation->RemoveAllEventHandlers();
        pAutomation->Release();
        CoUninitialize();
    }
} // namespace
#endif

/**
 * @brief [UiaClientTest] 같은 프로세스의 UIA 클라이언트가 진짜 창에 단 브리지를 통해 메뉴를 읽고 조작한다 — 이름 · 컨트롤 타입(단추 50000 · 슬라이더 50015), Invoke 가 단추 클릭이 되고,
 *        RangeValue 로 슬라이더 값이 바뀌고(범위 0 ~ 10), 체크 상자의 켬 상태와 꺼진 단추의 비활성이 보이고, 포커스가 옮겨 간 위젯의 이름이 알림으로 온다
 * @details ScreenReaderSupport A6(내레이터 실기 확인)의 자동화 몫이다 — 내레이터가 쓰는 같은 UIA 클라이언트 길(`IUIAutomation`)이라 제공자 쪽 모양(스레드 넘김 · 패턴 · 이름 · 알림)을 사람 귀 없이 본다.
 *          클라이언트 호출은 메인 스레드(창 메시지 펌프)로 넘어와 답하므로 클라이언트는 다른 스레드에서 돈다. 변이: 제공자가 Invoke 패턴을 내지 않으면 클릭 수가 0 이라 진다.
 */
SW_TEST_CASE( UiaClientTest, ClientReadsAndDrivesTheMenuThroughTheRealWindow )
{
#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    const HRESULT comInit  = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
    const bool    bOwnsCom = SUCCEEDED( comInit );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [bOwnsCom]()
    {
        if ( bOwnsCom )
            CoUninitialize();
    } ) );

    sw::unique_ptr<sw::IWindow> window = sw::IWindow::createPlatformWindow();
    SW_ASSERT_TRUE( window != nullptr );
    SW_ASSERT_TRUE( window->initializeWindow( "UiaClientTestWindow", 640, 360 ) );
    window->showWindow( true );
    window->processMessages();

    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ 640.0f, 360.0f };
    viewport._physicalSize = viewport._size;

    sw::unique_ptr<sw::uitest::TestPanelWidget> root     = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::unique_ptr<sw::ButtonWidget>            play     = sw::make_unique<sw::ButtonWidget>();
    sw::ButtonWidget*                           pPlay    = play.get();
    sw::unique_ptr<sw::TextWidget>              playText = sw::make_unique<sw::TextWidget>();
    playText->setText( "Play" );
    (void)play->addChild( std::move( playText ) );
    (void)root->addChild( std::move( play ) );
    sw::unique_ptr<sw::SliderWidget> slider = sw::make_unique<sw::SliderWidget>();
    slider->setRange( 0.0f, 10.0f, 1.0f );
    slider->setValue( 3.0f );
    slider->setAccessibleName( "Volume" );
    sw::SliderWidget* pSlider = slider.get();
    (void)root->addChild( std::move( slider ) );
    sw::unique_ptr<sw::CheckBoxWidget> mute = sw::make_unique<sw::CheckBoxWidget>();
    mute->setChecked( true );
    sw::unique_ptr<sw::TextWidget> muteText = sw::make_unique<sw::TextWidget>();
    muteText->setText( "Mute" );
    (void)mute->addChild( std::move( muteText ) );
    (void)root->addChild( std::move( mute ) );
    sw::unique_ptr<sw::ButtonWidget> quit = sw::make_unique<sw::ButtonWidget>();
    quit->setEnabled( false );
    sw::unique_ptr<sw::TextWidget> quitText = sw::make_unique<sw::TextWidget>();
    quitText->setText( "Quit" );
    (void)quit->addChild( std::move( quitText ) );
    (void)root->addChild( std::move( quit ) );
    const sw::UIScreenDesc   desc{};
    const sw::UIScreenHandle handle  = ui.pushScreen( sw::make_unique<sw::UIScreen>( desc, std::move( root ) ) );
    sw::UIScreen*            pScreen = ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    ui.setAccessibilityBridge( sw::UiaAccessibility::createBridge( ui, *window ) );
    // 클라이언트가 붙으면 첫 프레임에 이 포커스가 알림으로 간다.
    SW_ASSERT_TRUE( ui.getInputRouter().getFocusManager().setFocus( pScreen->getTree(), pSlider->getID() ) );
    ui.update( 1.0f / 60.0f, viewport );

    UiaFocusRecorder* pRecorder = sw_new UiaFocusRecorder();
    UiaClientObservation                 observed{};
    sw::atomic<bool>                     bClientDone{ false };
    sw::thread                           client( [&]()
    {
        runUiaClient( static_cast<HWND>( window->getNativeHandle() ), pRecorder, &observed );
        bClientDone.store( true );
    } );

    // 메인 스레드: UIA 가 넘기는 제공자 호출을 받도록 창 메시지를 돌리고 UI 를 갱신한다(알림은 갱신에서 나간다).
    const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 30000 );
    while ( bClientDone.load() == false && deadline.isExpired() == false )
    {
        (void)window->processMessages();
        ui.update( 1.0f / 60.0f, viewport );
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    SW_EXPECT_TRUE_MSG( bClientDone.load(), "the UIA client did not finish in time (a provider call may be stuck)" );
    client.join();

    SW_EXPECT_TRUE_MSG( observed._error.empty(), observed._error.c_str() );
    SW_ASSERT_TRUE_MSG( observed._bConnected, "the UIA client could not reach the window" );
    SW_EXPECT_TRUE( observed._bButtonFound );
    SW_EXPECT_EQUAL( 50000, observed._buttonControlType );
    SW_EXPECT_TRUE( observed._bInvoked );
    SW_EXPECT_EQUAL( 1u, pPlay->getClickCount() ); // Invoke 가 사람의 클릭과 같은 길로 단추를 눌렀다
    SW_EXPECT_TRUE( observed._bSliderFound );
    SW_EXPECT_EQUAL( 50015, observed._sliderControlType );
    SW_EXPECT_NEAR_EQUAL( 3.0, observed._sliderValueBefore, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 10.0, observed._sliderMaximum, 1e-6 );
    SW_EXPECT_TRUE( observed._bSliderSet );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pSlider->getValue(), 1e-6f );
    SW_EXPECT_TRUE( observed._bCheckFound );
    SW_EXPECT_EQUAL( static_cast<int32>( ToggleState_On ), observed._checkToggleState );
    SW_EXPECT_TRUE( observed._bDisabledSeen );
    SW_EXPECT_FALSE( observed._bDisabledEnabledProperty );
    SW_EXPECT_STREQ( "Volume", pRecorder->getLastName().c_str() ); // 포커스가 있던 슬라이더의 이름이 포커스 알림으로 왔다

    pRecorder->Release();
    ui.setAccessibilityBridge( nullptr ); // 창보다 먼저 브리지를 뺀다
    window->destroy();
#else
    SW_TEST_SKIP( "UI Automation exists only in Windows client builds" );
#endif
}
