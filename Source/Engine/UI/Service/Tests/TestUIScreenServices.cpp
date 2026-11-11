// UIScreenServicesTest — 화면 없이 도는 UI 서비스 규칙: 툴팁 자리 · 커서 · 주인 찾기, 자막의 읽는 시간 · 크기 · 줄 차례, 화면의 포커스 규칙 · 명령 표(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Service/UISubtitleService.h"
#include "Engine/UI/Service/UITooltipService.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"
#include "Engine/UI/WidgetTree/UIWidgetPath.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

using namespace sw;

namespace
{
    struct UICommandProbe
    {
        Subscription _subscription{};
        int32        _count{ 0 };

        void onCommand( const hashed_string& /*command*/, Widget& /*source*/ ) { ++_count; }

        /** @brief 명령 안에서 자기 등록을 풉니다(버튼이 자기 화면의 명령을 끊는 경우). */
        void onCommandReleasingItself( const hashed_string& /*command*/, Widget& /*source*/ )
        {
            _subscription.reset();
            ++_count;
        }
    };

    /** @brief 잎 하나를 가진 화면입니다. 시스템에 붙이지 않아 전역 명령표는 묻지 않습니다. */
    struct UIScreenFixture
    {
        UIScreen _screen;
        Widget*  _pLeaf;

        explicit UIScreenFixture( const UIScreenDef& desc = UIScreenDef{} )
            : _screen{ desc, make_unique<CanvasPanel>() }
            , _pLeaf{ nullptr }
        {
            _pLeaf = &UITestUtil::addLeaf( *castTo<PanelWidget>( _screen.getTree().getRoot() ), float2{ 10.0f, 10.0f } );
        }
    };
} // namespace

/** @brief [UIScreenServicesTest] 툴팁은 포인터 오른쪽 아래가 기본이고, 넘치면 그 축만 뒤집으며, 화면보다 크면 왼쪽 위 변에 붙는다 */
SW_TEST_CASE( UIScreenServicesTest, TooltipPlacement )
{
    const float2      screen{ 800.0f, 600.0f };
    const float2      size{ 100.0f, 50.0f };
    constexpr float32 kOffsetX = UITooltipService::kPointerOffsetX;
    constexpr float32 kOffsetY = UITooltipService::kPointerOffsetY;

    float2 placed = UITooltipService::computePlacement( float2{ 10.0f, 10.0f }, size, screen );
    SW_EXPECT_NEAR( 10.0f + kOffsetX, placed._x, 1.0e-4f );
    SW_EXPECT_NEAR( 10.0f + kOffsetY, placed._y, 1.0e-4f );

    placed = UITooltipService::computePlacement( float2{ 790.0f, 10.0f }, size, screen );
    SW_EXPECT_NEAR( 790.0f - kOffsetX - 100.0f, placed._x, 1.0e-4f );
    SW_EXPECT_NEAR( 10.0f + kOffsetY, placed._y, 1.0e-4f );

    placed = UITooltipService::computePlacement( float2{ 10.0f, 590.0f }, size, screen );
    SW_EXPECT_NEAR( 10.0f + kOffsetX, placed._x, 1.0e-4f );
    SW_EXPECT_NEAR( 590.0f - kOffsetY - 50.0f, placed._y, 1.0e-4f );

    placed = UITooltipService::computePlacement( float2{ 400.0f, 300.0f }, float2{ 2000.0f, 2000.0f }, screen );
    SW_EXPECT_NEAR( 0.0f, placed._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, placed._y, 1.0e-4f );
}

/** @brief [UIScreenServicesTest] 커서와 툴팁 주인은 경로의 잎에서 루트 쪽으로 처음 정한 위젯을 따른다. 모두 부모를 따르면 화살표 · 주인 없음이다 */
SW_TEST_CASE( UIScreenServicesTest, CursorAndTooltipOwnerWalkThePath )
{
    WidgetTree   tree;
    CanvasPanel& root  = UITestUtil::setRoot<CanvasPanel>( tree );
    CanvasPanel& panel = UITestUtil::addPanel<CanvasPanel>( root );
    UITestLeaf&  leaf  = UITestUtil::addLeaf( panel, float2{ 10.0f, 10.0f } );

    UIWidgetPath path;
    path._listWidget = { root.getID(), panel.getID(), leaf.getID() };

    SW_EXPECT_TRUE( UITooltipService::resolveCursorShape( tree, path ) == UICursorShape::Arrow );
    SW_EXPECT_TRUE( UITooltipService::findTooltipOwner( tree, path ) == nullptr );

    panel.setCursorShape( UICursorShape::Hand );
    panel.setTooltip( "panel" );
    SW_EXPECT_TRUE( UITooltipService::resolveCursorShape( tree, path ) == UICursorShape::Hand );
    SW_EXPECT_TRUE( UITooltipService::findTooltipOwner( tree, path ) == &panel );

    leaf.setCursorShape( UICursorShape::TextBeam );
    leaf.setTooltip( "leaf" );
    SW_EXPECT_TRUE( UITooltipService::resolveCursorShape( tree, path ) == UICursorShape::TextBeam );
    SW_EXPECT_TRUE( UITooltipService::findTooltipOwner( tree, path ) == &leaf );
    SW_EXPECT_TRUE( UITooltipService::toCursorShape( UICursorShape::TextBeam ) == CursorShape::TextBeam );

    path._listWidget.push_back( kInvalidWidgetID ); // 지난 프레임의 경로에 사라진 위젯이 있어도 건너뛴다
    SW_EXPECT_TRUE( UITooltipService::findTooltipOwner( tree, path ) == &leaf );
}

/** @brief [UIScreenServicesTest] 읽는 시간은 바이트가 아니라 글자 수로 세고 하한이 있다. 크기 설정은 범위 안으로 자른다 */
SW_TEST_CASE( UIScreenServicesTest, SubtitleReadingTimeAndSize )
{
    SW_EXPECT_NEAR( UISubtitleService::kMinReadingSeconds, UISubtitleService::computeReadingSeconds( "" ), 1.0e-5f );
    const string longLatin( 100, 'a' );
    SW_EXPECT_NEAR( 100.0f * UISubtitleService::kReadingSecondsPerCharacter, UISubtitleService::computeReadingSeconds( longLatin ), 1.0e-4f );

    string longKorean;
    for ( int32 index = 0; index < 100; ++index )
    {
        longKorean += "가";
    }
    SW_EXPECT_NEAR( 100.0f * UISubtitleService::kReadingSecondsPerCharacter, UISubtitleService::computeReadingSeconds( longKorean ), 1.0e-4f );

    SW_EXPECT_NEAR( UISubtitleService::computeSizeScale( 0 ), UISubtitleService::computeSizeScale( -5 ), 1.0e-5f );
    SW_EXPECT_NEAR( UISubtitleService::computeSizeScale( 2 ), UISubtitleService::computeSizeScale( 99 ), 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, UISubtitleService::computeSizeScale( 1 ), 1.0e-5f );
}

/** @brief [UIScreenServicesTest] 자막 차례: 두 줄까지 보이고 나머지는 기다린다. 기다리는 동안 시간이 흐르지 않고, 앞줄이 끝나면 먼저 온 줄이 올라온다. 꺼 두어도 시간은 흐른다 */
SW_TEST_CASE( UIScreenServicesTest, SubtitleQueueOrder )
{
    const bool bSaved = gv_subtitles;
    gv_subtitles      = false; // 화면을 열지 않고 차례만 본다
    {
        UISubtitleService service;
        const uint32      firstID = service.post( "", "first", 1.0f );
        (void)service.post( "", "second", 3.0f );
        (void)service.post( "", "third", 1.0f );
        SW_EXPECT_EQUAL( uint32( 3 ), service.getQueuedLineCount() );

        service.update( 0.1f );
        SW_EXPECT_EQUAL( UISubtitleService::kMaxVisibleLineCount, service.getActiveLineCount() );
        SW_EXPECT_EQUAL( uint32( 1 ), service.getQueuedLineCount() );

        service.update( 1.0f ); // 첫 줄이 끝났다
        SW_EXPECT_EQUAL( uint32( 2 ), service.getActiveLineCount() );
        SW_EXPECT_EQUAL( uint32( 0 ), service.getQueuedLineCount() );

        service.update( 1.0f ); // 세 번째 줄은 올라온 뒤로 1 초를 다 썼다
        SW_EXPECT_EQUAL( uint32( 1 ), service.getActiveLineCount() );

        service.update( 2.0f );
        SW_EXPECT_EQUAL( uint32( 0 ), service.getActiveLineCount() );

        const uint32 nextID = service.post( "", "again", 0.0f ); // 0 이면 읽는 시간으로 정한다
        SW_EXPECT_FALSE( nextID == firstID );
        service.update( UISubtitleService::kMinReadingSeconds - 0.5f );
        SW_EXPECT_EQUAL( uint32( 1 ), service.getActiveLineCount() );

        service.clear();
        SW_EXPECT_EQUAL( uint32( 0 ), service.getActiveLineCount() );
        SW_EXPECT_EQUAL( uint32( 0 ), service.getQueuedLineCount() );
    }
    gv_subtitles = bSaved;
}

/** @brief [UIScreenServicesTest] HUD 와 오버레이는 설정과 상관없이 포커스를 받지 않고, 시스템에 붙지 않은 화면을 닫으면 닫는 중이 된다 */
SW_TEST_CASE( UIScreenServicesTest, ScreenFocusRuleAndClose )
{
    UIScreenDef desc;
    desc._layer       = UILayer::HUD;
    desc._bTakesFocus = true;
    SW_EXPECT_FALSE( UIScreenFixture( desc )._screen.canTakeFocus() );
    desc._layer = UILayer::Overlay;
    SW_EXPECT_FALSE( UIScreenFixture( desc )._screen.canTakeFocus() );
    desc._layer = UILayer::Menu;
    SW_EXPECT_TRUE( UIScreenFixture( desc )._screen.canTakeFocus() );
    desc._bTakesFocus = false;
    SW_EXPECT_FALSE( UIScreenFixture( desc )._screen.canTakeFocus() );

    UIScreenFixture rig;
    SW_EXPECT_FALSE( rig._screen.isClosing() );
    SW_EXPECT_TRUE( rig._screen.onBack() );
    SW_EXPECT_TRUE( rig._screen.isClosing() );
}

/** @brief [UIScreenServicesTest] 명령 표: 등록한 함수를 부르고, 구독을 풀면 사라진다. 같은 이름을 다시 등록한 뒤 옛 구독을 풀어도 새 등록은 남는다 */
SW_TEST_CASE( UIScreenServicesTest, CommandSubscriptions )
{
    SW_EXPECT_LOG( "UIScreen.CommandWidgetNotHandled" );
    UIScreenFixture     rig;
    UICommandProbe      first;
    UICommandProbe      second;
    const hashed_string command( "Do" );
    first._subscription = rig._screen.registerCommand( command, UICommandDelegate::create<&UICommandProbe::onCommand>( &first ) );
    rig._screen.dispatchCommand( command, *rig._pLeaf );
    SW_EXPECT_EQUAL( 1, first._count );

    second._subscription = rig._screen.registerCommand( command, UICommandDelegate::create<&UICommandProbe::onCommand>( &second ) );
    first._subscription.reset(); // 옛 구독이다
    rig._screen.dispatchCommand( command, *rig._pLeaf );
    SW_EXPECT_EQUAL( 1, first._count );
    SW_EXPECT_EQUAL( 1, second._count );

    second._subscription.reset();
    rig._screen.dispatchCommand( command, *rig._pLeaf ); // 이제 아무도 받지 않아 경고가 남는다
    SW_EXPECT_EQUAL( 1, second._count );

    rig._screen.dispatchCommand( hashed_string{}, *rig._pLeaf ); // 빈 명령은 조용히 지나간다
}

/** @brief [UIScreenServicesTest] 명령 함수가 자기 등록을 풀어도(표에서 지워져도) 돌고 있는 함수는 끝까지 돈다 */
SW_TEST_CASE( UIScreenServicesTest, CommandMayReleaseItself )
{
    UIScreenFixture     rig;
    UICommandProbe      probe;
    const hashed_string command( "Once" );
    probe._subscription = rig._screen.registerCommand( command, UICommandDelegate::create<&UICommandProbe::onCommandReleasingItself>( &probe ) );
    rig._screen.dispatchCommand( command, *rig._pLeaf );
    SW_EXPECT_EQUAL( 1, probe._count );
    SW_EXPECT_FALSE( rig._screen.onCommand( command, *rig._pLeaf ) );
}
