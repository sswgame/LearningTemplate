#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/UI/Animation/UiAnimation.h"
#include "Engine/UI/Animation/UiAnimationPlayer.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"

#include "EngineTest/UI/UiLayoutTestUtil.h"

#include "TestFramework/TestFramework.h"

// UiAnimationTest — 프로퍼티 트윈 · UI 애니메이션(트랙 · 키 · BlendCurve · 사건 · 반복 · 거꾸로 · 열기/닫기) · 움직임 줄이기. 디바이스 없음(nogpu).
// 문서는 메모리로 넣는다(UiDocumentCache::registerMemoryDocument). 시간은 프레임 시간을 손으로 준다.

namespace
{
    /** @brief 전역 bool 변수 하나를 이름으로 바꾸고 스코프 끝에 되돌립니다(시험 DLL 은 Engine 의 gv_* 를 extern 으로 못 읽는다). */
    class ScopedBoolVariable
    {
    public:
        ScopedBoolVariable( const utf8* pName, bool bValue )
            : _pInfo{ sw::engine::getGlobalVariableManager().findVariable( pName ) }
            , _bPrevious{ false }
        {
            SW_EXPECT_TRUE( _pInfo != nullptr );
            if ( _pInfo == nullptr )
                return;
            _bPrevious = _pInfo->getValueAsBool();
            (void)_pInfo->setValueAsBool( bValue );
        }
        ~ScopedBoolVariable()
        {
            if ( _pInfo != nullptr )
                (void)_pInfo->setValueAsBool( _bPrevious );
        }
        ScopedBoolVariable( const ScopedBoolVariable& )            = delete;
        ScopedBoolVariable& operator=( const ScopedBoolVariable& ) = delete;

    private:
        sw::GlobalVariableInfo* _pInfo;
        bool                    _bPrevious;
    };

    struct UiAnimationTestUtil
    {
        static constexpr float32 kTolerance = 0.001f;

        /** @brief 트랙 하나(키 둘 — 0 초 @p pFrom, @p endTime 초 @p pTo, Linear)인 애니메이션입니다. */
        static sw::UiAnimation makeAnimation( const utf8* pName, const utf8* pWidget, const utf8* pProperty, const utf8* pFrom, const utf8* pTo, float32 endTime )
        {
            sw::UiAnimationTrack track{};
            track._widget   = sw::hashed_string( pWidget );
            track._property = pProperty;
            track._listKey.push_back( sw::UiAnimationKey{ 0.0f, pFrom, sw::BlendCurve::Linear } );
            track._listKey.push_back( sw::UiAnimationKey{ endTime, pTo, sw::BlendCurve::Linear } );
            sw::UiAnimation animation{};
            animation._name = sw::hashed_string( pName );
            animation._listTrack.push_back( std::move( track ) );
            return animation;
        }
    };

    /** @brief 가로 상자 아래 고정 위젯 "a"(100×20) 하나인 트리와 그 재생기 — 레이아웃 수를 센다. */
    struct UiAnimationLayoutFixture
    {
        sw::test::UiLayoutFixture  _layout;
        sw::UiAnimationPlayer      _player;
        sw::test::TestFixedWidget* _pWidget;

        UiAnimationLayoutFixture()
            : _layout{ 800.0f, 600.0f }
            , _player{ _layout.getTree() }
            , _pWidget{ nullptr }
        {
            sw::BoxPanel* pRow = _layout.setRoot<sw::BoxPanel>( "row" );
            _pWidget           = _layout.addFixed( pRow, "a", 100.0f, 20.0f );
            (void)_layout.update();
        }
    };

    /** @brief 입력 관리자와 UI 시스템 — 메모리 문서를 연다. */
    struct UiAnimationScreenFixture
    {
        sw::InputManager _input;
        sw::UiSystem     _ui;

        UiAnimationScreenFixture()
            : _input{}
            , _ui{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
        }

        ~UiAnimationScreenFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UiAnimationScreenFixture( const UiAnimationScreenFixture& )            = delete;
        UiAnimationScreenFixture& operator=( const UiAnimationScreenFixture& ) = delete;

        /** @brief 애니메이션 목록 @p pAnimations(`<_listAnimation>` 원소, 비면 없음)과 테두리 패널 "Box" 하나인 문서를 엽니다. */
        sw::UiScreenHandle open( const utf8* pPath, const utf8* pAnimations )
        {
            const sw::string document = sw::string( "<UiDocument _schemaVersion=\"1\">\n" ) + pAnimations +
                                        "\t<CanvasPanel>\n"
                                        "\t\t<BorderPanel _name=\"Box\" />\n"
                                        "\t</CanvasPanel>\n"
                                        "</UiDocument>\n";
            _ui.getDocumentCache().registerMemoryDocument( pPath, document );
            return _ui.openScreen( pPath );
        }

        void runFrame( float32 deltaSeconds )
        {
            _input.beginFrame( deltaSeconds );
            _ui.processInput( deltaSeconds );
            _ui.update( deltaSeconds, sw::UiViewport{
                                          sw::float2{ 1280.0f, 720.0f }
            } );
            _input.endFrame();
        }
    };

    /** @brief 0.5 초 동안 Box 의 불투명도를 0 → 1(Open) · 1 → 0(Close) 로 옮기는 문서 애니메이션입니다. */
    constexpr utf8 kOpenCloseAnimations[] = "\t<_listAnimation>\n"
                                            "\t\t<UiAnimation _name=\"Open\">\n"
                                            "\t\t\t<_listTrack>\n"
                                            "\t\t\t\t<UiAnimationTrack _widget=\"Box\" _property=\"_opacity\">\n"
                                            "\t\t\t\t\t<_listKey>\n"
                                            "\t\t\t\t\t\t<UiAnimationKey _time=\"0\" _value=\"0\" _curve=\"Linear\" />\n"
                                            "\t\t\t\t\t\t<UiAnimationKey _time=\"0.5\" _value=\"1\" _curve=\"Linear\" />\n"
                                            "\t\t\t\t\t</_listKey>\n"
                                            "\t\t\t\t</UiAnimationTrack>\n"
                                            "\t\t\t</_listTrack>\n"
                                            "\t\t</UiAnimation>\n"
                                            "\t\t<UiAnimation _name=\"Close\">\n"
                                            "\t\t\t<_listTrack>\n"
                                            "\t\t\t\t<UiAnimationTrack _widget=\"Box\" _property=\"_opacity\">\n"
                                            "\t\t\t\t\t<_listKey>\n"
                                            "\t\t\t\t\t\t<UiAnimationKey _time=\"0\" _value=\"1\" _curve=\"Linear\" />\n"
                                            "\t\t\t\t\t\t<UiAnimationKey _time=\"0.5\" _value=\"0\" _curve=\"Linear\" />\n"
                                            "\t\t\t\t\t</_listKey>\n"
                                            "\t\t\t\t</UiAnimationTrack>\n"
                                            "\t\t\t</_listTrack>\n"
                                            "\t\t</UiAnimation>\n"
                                            "\t</_listAnimation>\n";
} // namespace

/** @brief [UiAnimationTest] 트윈은 지금 값에서 끝값으로 곡선을 따라 옮긴다 — Linear 는 반에서 반, EaseIn(지수 2)은 반에서 1/4, 끝나면 끝값 · 트윈 없음 */
SW_TEST_CASE( UiAnimationTest, TweenInterpolatesWithCurve )
{
    using Util = UiAnimationTestUtil;
    UiAnimationScreenFixture fixture;
    sw::UiScreen*            pScreen = fixture._ui.findScreen( fixture.open( "test/tween.ui.xml", "" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );
    fixture.runFrame( 0.0f );

    SW_ASSERT_TRUE( fixture._ui.tween( pBox->getId(), "_opacity", "0", 1.0f, sw::BlendCurve::Linear ) );
    SW_ASSERT_TRUE( fixture._ui.tween( pBox->getId(), "_renderTransform._translation", "100,0", 1.0f, sw::BlendCurve::EaseIn ) );
    fixture.runFrame( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBox->getOpacity(), Util::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 25.0f, pBox->getRenderTransform()._translation._x, Util::kTolerance );
    fixture.runFrame( 0.6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBox->getOpacity(), Util::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pBox->getRenderTransform()._translation._x, Util::kTolerance );
    SW_EXPECT_EQUAL( 0u, pScreen->getAnimationPlayer().getTweenCount() );

    // 모르는 경로 · 읽지 못하는 값은 걸지 않는다
    SW_EXPECT_FALSE( fixture._ui.tween( pBox->getId(), "_noSuchProperty", "1", 1.0f ) );
    SW_EXPECT_FALSE( fixture._ui.tween( pBox->getId(), "_opacity", "not a number", 1.0f ) );
}

/** @brief [UiAnimationTest] 렌더 변환 트랙은 레이아웃을 다시 재지 않는다(measure 0) — 그래도 기하는 움직인다 */
SW_TEST_CASE( UiAnimationTest, TransformTrackDoesNotRelayout )
{
    using Util = UiAnimationTestUtil;
    UiAnimationLayoutFixture    fixture;
    const float32               startX = fixture._pWidget->getGeometry()._translation._x;
    sw::vector<sw::UiAnimation> listAnimation;
    listAnimation.push_back( Util::makeAnimation( "Slide", "a", "_renderTransform._translation", "0,0", "100,0", 1.0f ) );
    fixture._player.setAnimations( std::move( listAnimation ) );
    SW_ASSERT_TRUE( fixture._player.play( "Slide" ) );

    uint32 measuredCount = fixture._layout.update(); // 첫 키 값을 쓴 것
    for ( uint32 frame = 0; frame < 2; ++frame )
    {
        fixture._player.tick( 0.25f );
        measuredCount += fixture._layout.update();
    }
    SW_EXPECT_EQUAL( 0u, measuredCount );
    SW_EXPECT_EQUAL( 1u, fixture._pWidget->getMeasureCount() ); // 처음 짓기의 한 번뿐
    SW_EXPECT_NEAR_EQUAL( startX + 50.0f, fixture._pWidget->getGeometry()._translation._x, Util::kTolerance );
}

/** @brief [UiAnimationTest] 크기(슬롯 너비 덮어쓰기) 트랙은 매 프레임 레이아웃을 돌린다 — 크기 · 여백을 움직이면 비싸다 */
SW_TEST_CASE( UiAnimationTest, SizeTrackRelayouts )
{
    using Util = UiAnimationTestUtil;
    UiAnimationLayoutFixture    fixture;
    sw::vector<sw::UiAnimation> listAnimation;
    listAnimation.push_back( Util::makeAnimation( "Grow", "a", "_slot._widthOverride", "100", "200", 1.0f ) );
    fixture._player.setAnimations( std::move( listAnimation ) );
    SW_ASSERT_TRUE( fixture._player.play( "Grow" ) );
    (void)fixture._layout.update();

    fixture._player.tick( 0.5f );
    SW_EXPECT_TRUE( fixture._layout.update() > 0u );
    SW_EXPECT_NEAR_EQUAL( 150.0f, fixture._pWidget->getGeometry()._size._x, Util::kTolerance );
}

/** @brief [UiAnimationTest] 사건은 재생이 그 시각을 지날 때 한 번 — 처음(0) · 가운데 · 끝 시각 모두, 짧은 프레임이 여럿이어도 한 번 */
SW_TEST_CASE( UiAnimationTest, EventsFireOnce )
{
    UiAnimationScreenFixture fixture;
    sw::UiScreen*            pScreen = fixture._ui.findScreen( fixture.open( "test/events.ui.xml", "" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    uint32 startCount  = 0;
    uint32 middleCount = 0;
    uint32 endCount    = 0;
    pScreen->registerCommand( "Start", SW_DELEGATE_LAMBDA( sw::UiCommandDelegate, [&startCount]( const sw::hashed_string&, sw::Widget& )
    { ++startCount; } ) );
    pScreen->registerCommand( "Middle", SW_DELEGATE_LAMBDA( sw::UiCommandDelegate, [&middleCount]( const sw::hashed_string&, sw::Widget& )
    { ++middleCount; } ) );
    pScreen->registerCommand( "End", SW_DELEGATE_LAMBDA( sw::UiCommandDelegate, [&endCount]( const sw::hashed_string&, sw::Widget& )
    { ++endCount; } ) );

    sw::UiAnimation animation{};
    animation._name = "Pulse";
    animation._listEvent.push_back( sw::UiAnimationEvent{ 0.0f, "Start" } );
    animation._listEvent.push_back( sw::UiAnimationEvent{ 0.5f, "Middle" } );
    animation._listEvent.push_back( sw::UiAnimationEvent{ 1.0f, "End" } );
    sw::vector<sw::UiAnimation> listAnimation;
    listAnimation.push_back( std::move( animation ) );
    pScreen->getAnimationPlayer().setAnimations( std::move( listAnimation ) );
    SW_ASSERT_TRUE( pScreen->getAnimationPlayer().play( "Pulse" ) );

    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        fixture.runFrame( 0.05f ); // 1.5 초 — 0.5 · 1.0 에 프레임 경계가 닿는다
    }
    SW_EXPECT_EQUAL( 1u, startCount );
    SW_EXPECT_EQUAL( 1u, middleCount );
    SW_EXPECT_EQUAL( 1u, endCount );
    SW_EXPECT_FALSE( pScreen->getAnimationPlayer().isPlaying( "Pulse" ) );
}

/** @brief [UiAnimationTest] 거꾸로 재생은 끝에서 처음으로, 재생 중 reverse 는 그 자리에서 돌아서고, 반복은 바퀴마다 처음부터 */
SW_TEST_CASE( UiAnimationTest, ReverseAndLoop )
{
    using Util = UiAnimationTestUtil;
    UiAnimationLayoutFixture    fixture;
    sw::vector<sw::UiAnimation> listAnimation;
    listAnimation.push_back( Util::makeAnimation( "Fade", "a", "_opacity", "0", "1", 1.0f ) );
    fixture._player.setAnimations( std::move( listAnimation ) );

    // 거꾸로 — 시작 값은 끝 키(1), 0.25 초 뒤 0.75
    SW_ASSERT_TRUE( fixture._player.playReverse( "Fade" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._pWidget->getOpacity(), Util::kTolerance );
    fixture._player.tick( 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, fixture._pWidget->getOpacity(), Util::kTolerance );

    // 재생 중 돌아서기 — 0.75 에서 다시 위로
    fixture._player.reverse( "Fade" );
    fixture._player.tick( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.85f, fixture._pWidget->getOpacity(), Util::kTolerance );
    fixture._player.tick( 1.0f );
    SW_EXPECT_FALSE( fixture._player.isPlaying( "Fade" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._pWidget->getOpacity(), Util::kTolerance );

    // 세 바퀴 — 바퀴마다 다시 0 에서, 셋째 바퀴 끝에서 멈춘다
    SW_ASSERT_TRUE( fixture._player.play( "Fade", 2.0f, 3 ) ); // 두 배속 — 한 바퀴 0.5 초
    fixture._player.tick( 0.6f );                              // 둘째 바퀴의 0.2
    SW_EXPECT_TRUE( fixture._player.isPlaying( "Fade" ) );
    SW_EXPECT_NEAR_EQUAL( 0.2f, fixture._pWidget->getOpacity(), Util::kTolerance );
    fixture._player.tick( 0.8f ); // 1.4 초 — 셋째 바퀴 0.8
    SW_EXPECT_TRUE( fixture._player.isPlaying( "Fade" ) );
    fixture._player.tick( 0.2f );
    SW_EXPECT_FALSE( fixture._player.isPlaying( "Fade" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fixture._pWidget->getOpacity(), Util::kTolerance );
}

/** @brief [UiAnimationTest] Open 은 올릴 때 첫 키 값부터, Close 는 닫기를 미루고 끝난 뒤 화면을 지운다(그동안 활성 화면이 아니다) */
SW_TEST_CASE( UiAnimationTest, CloseWaitsForCloseAnimation )
{
    using Util = UiAnimationTestUtil;
    UiAnimationScreenFixture fixture;
    const sw::UiScreenHandle handle  = fixture.open( "test/openclose.ui.xml", kOpenCloseAnimations );
    sw::UiScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBox->getOpacity(), Util::kTolerance ); // 올리는 순간 첫 키(번쩍임 없음)
    SW_EXPECT_TRUE( pScreen->getAnimationPlayer().isPlaying( "Open" ) );
    fixture.runFrame( 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBox->getOpacity(), Util::kTolerance );
    fixture.runFrame( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBox->getOpacity(), Util::kTolerance );

    fixture._ui.closeScreen( handle );
    fixture.runFrame( 0.25f );
    SW_ASSERT_TRUE( fixture._ui.findScreen( handle ) != nullptr ); // 닫기 애니메이션 중 — 아직 있다
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() != pScreen );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBox->getOpacity(), Util::kTolerance );
    fixture.runFrame( 0.3f );
    SW_EXPECT_TRUE( fixture._ui.findScreen( handle ) == nullptr );
}

/** @brief [UiAnimationTest] 움직임 줄이기(gv_uiReduceMotion)면 애니메이션 · 트윈 · 닫기가 바로 끝 값이다 */
SW_TEST_CASE( UiAnimationTest, ReduceMotionJumpsToEnd )
{
    using Util = UiAnimationTestUtil;
    const ScopedBoolVariable reduceMotion( "gv_uiReduceMotion", true );
    UiAnimationScreenFixture fixture;
    const sw::UiScreenHandle handle  = fixture.open( "test/reduce.ui.xml", kOpenCloseAnimations );
    sw::UiScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBox->getOpacity(), Util::kTolerance ); // Open 의 끝 값
    SW_EXPECT_FALSE( pScreen->getAnimationPlayer().isAnyPlaying() );

    SW_ASSERT_TRUE( fixture._ui.tween( pBox->getId(), "_opacity", "0.25", 2.0f ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pBox->getOpacity(), Util::kTolerance );
    SW_EXPECT_EQUAL( 0u, pScreen->getAnimationPlayer().getTweenCount() );

    fixture._ui.closeScreen( handle );
    fixture.runFrame( 0.0f );
    SW_EXPECT_TRUE( fixture._ui.findScreen( handle ) == nullptr );
}

/** @brief [UiAnimationTest] 같은 위젯 · 경로의 새 트윈은 옛 것을 대신한다 — 지금 값에서 새 끝값으로 */
SW_TEST_CASE( UiAnimationTest, NewTweenReplacesOld )
{
    using Util = UiAnimationTestUtil;
    UiAnimationScreenFixture fixture;
    sw::UiScreen*            pScreen = fixture._ui.findScreen( fixture.open( "test/replace.ui.xml", "" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    sw::Widget* pBox = pScreen->getTree().findWidgetByName( "Box" );
    SW_ASSERT_NOT_NULL( pBox );

    SW_ASSERT_TRUE( fixture._ui.tween( pBox->getId(), "_opacity", "0", 1.0f, sw::BlendCurve::Linear ) );
    fixture.runFrame( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBox->getOpacity(), Util::kTolerance );
    SW_ASSERT_TRUE( fixture._ui.tween( pBox->getId(), "_opacity", "1", 1.0f, sw::BlendCurve::Linear ) );
    SW_EXPECT_EQUAL( 1u, pScreen->getAnimationPlayer().getTweenCount() );
    fixture.runFrame( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, pBox->getOpacity(), Util::kTolerance );
}

/** @brief [UiAnimationTest] 문서의 애니메이션 — 이름 없음 · 겹친 이름 · 모르는 칸은 로드 오류(파일 · 줄) */
SW_TEST_CASE( UiAnimationTest, DocumentRejectsBadAnimations )
{
    const auto parse = []( const utf8* pAnimations, sw::string& outError )
    {
        const sw::string    text = sw::string( "<UiDocument _schemaVersion=\"1\">\n" ) + pAnimations + "\t<CanvasPanel />\n</UiDocument>\n";
        sw::UiDocumentAsset asset{};
        return sw::UiDocumentLoader::parse( text, "test/bad.ui.xml", asset, outError );
    };
    sw::string error;
    SW_EXPECT_TRUE( parse( kOpenCloseAnimations, error ) );
    SW_EXPECT_FALSE( parse( "\t<_listAnimation><UiAnimation /></_listAnimation>\n", error ) );
    SW_EXPECT_TRUE( error.find( "without _name" ) != sw::string::npos );
    SW_EXPECT_FALSE( parse( "\t<_listAnimation><UiAnimation _name=\"A\" /><UiAnimation _name=\"A\" /></_listAnimation>\n", error ) );
    SW_EXPECT_TRUE( error.find( "two animations named 'A'" ) != sw::string::npos );
    SW_EXPECT_FALSE( parse( "\t<_listAnimation><UiAnimation _name=\"A\" _speed=\"2\" /></_listAnimation>\n", error ) );
    SW_EXPECT_TRUE( error.find( "test/bad.ui.xml:2" ) != sw::string::npos );
}
