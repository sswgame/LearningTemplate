#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/SafeZonePanel.h"
#include "Engine/UI/Layout/UiScale.h"
#include "Engine/UI/UiSystem.h"

#include "EngineTest/UI/UiLayoutTestUtil.h"

#include "TestFramework/TestFramework.h"

// UiScaleTest — UI 배율(해상도 규칙 × gv_uiScale) · 글자 배율 · 안전 영역 · 창 콘텐츠 배율. 디바이스 없음(nogpu).

namespace
{
    /** @brief 전역 float 변수 하나를 이름으로 바꾸고 스코프 끝에 되돌립니다(시험 DLL 은 Engine 의 gv_* 를 extern 으로 못 읽는다). */
    class ScopedFloatVariable
    {
    public:
        ScopedFloatVariable( const utf8* pName, float32 value )
            : _pInfo{ sw::engine::getGlobalVariableManager().findVariable( pName ) }
            , _previous{ 0.0f }
        {
            SW_EXPECT_TRUE( _pInfo != nullptr );
            if ( _pInfo == nullptr )
                return;
            _previous = _pInfo->getValueAsFloat();
            (void)_pInfo->setValueAsFloat( value );
        }
        ~ScopedFloatVariable()
        {
            if ( _pInfo != nullptr )
                (void)_pInfo->setValueAsFloat( _previous );
        }
        ScopedFloatVariable( const ScopedFloatVariable& )            = delete;
        ScopedFloatVariable& operator=( const ScopedFloatVariable& ) = delete;

    private:
        sw::GlobalVariableInfo* _pInfo;
        float32                 _previous;
    };

    struct UiScaleTestUtil
    {
        /** @brief 가로 상자(간격 10) · 고정 위젯 둘을 짓고 @p uiScale 배율 뷰포트로 잽니다. */
        static void buildRow( sw::test::UiLayoutFixture& fixture, const sw::UiViewport& viewport )
        {
            fixture.getContext()._viewportSize = viewport._size;
            fixture.getContext()._uiScale      = viewport._uiScale;
            sw::BoxPanel* pRow                 = fixture.setRoot<sw::BoxPanel>( "row" );
            pRow->setSpacing( 10.0f );
            fixture.addFixed( pRow, "a", 50.0f, 20.0f );
            fixture.addFixed( pRow, "b", 70.0f, 40.0f );
            fixture.update();
        }
    };
} // namespace

/** @brief [UiScaleTest] 짧은 변 규칙: 1080p → 1, 2160p → 2, 720p → 0.6667, 세로 1080×1920 → 1, 최소 · 최대로 묶음 */
SW_TEST_CASE( UiScaleTest, ShortestSideRule )
{
    const sw::UiScaleSettings settings{};
    SW_EXPECT_NEAR_EQUAL( 1.0f, settings.computeResolutionScale( sw::float2{ 1920.0f, 1080.0f } ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, settings.computeResolutionScale( sw::float2{ 3840.0f, 2160.0f } ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.6667f, settings.computeResolutionScale( sw::float2{ 1280.0f, 720.0f } ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, settings.computeResolutionScale( sw::float2{ 1080.0f, 1920.0f } ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, settings.computeResolutionScale( sw::float2{ 200.0f, 100.0f } ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, settings.computeResolutionScale( sw::float2{ 10000.0f, 10000.0f } ), 0.0001f );
}

/** @brief [UiScaleTest] gv_uiScale 1.25 → 배율 1.25 배, UI 뷰포트 = 물리 / 배율 */
SW_TEST_CASE( UiScaleTest, UserScaleMultiplies )
{
    const ScopedFloatVariable userScale( "gv_uiScale", 1.25f );
    const sw::UiSystem        uiSystem;
    const sw::UiViewport      viewport = uiSystem.computeViewport( sw::float2{ 1920.0f, 1080.0f }, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.25f, viewport._uiScale, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 1536.0f, viewport._size._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 864.0f, viewport._size._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 1920.0f, viewport._physicalSize._x, 0.001f );
}

/** @brief [UiScaleTest] 같은 트리를 1 · 1.5 · 2 배 화면에 → UI 단위 덤프는 같고 물리 사각형만 배율만큼 */
SW_TEST_CASE( UiScaleTest, LayoutScalesUniformly )
{
    const sw::UiScaleSettings settings{};
    const float32             arrScale[] = { 1.0f, 1.5f, 2.0f };
    for ( const float32 scale : arrScale )
    {
        const sw::UiViewport viewport = sw::UiScaleUtil::makeViewport( settings, sw::float2{ 1920.0f * scale, 1080.0f * scale }, 1.0f, 1.0f, 0.0f );
        SW_EXPECT_NEAR_EQUAL( scale, viewport._uiScale, 0.0001f );
        sw::test::UiLayoutFixture fixture( 0.0f, 0.0f );
        UiScaleTestUtil::buildRow( fixture, viewport );
        SW_EXPECT_STREQ( "row 0.00 0.00 1920.00 1080.00\n"
                         "  a 0.00 0.00 50.00 1080.00\n"
                         "  b 60.00 0.00 70.00 1080.00\n",
                         fixture.dump().c_str() );
        if ( scale == 2.0f )
        {
            SW_EXPECT_STREQ( "row 0.00 0.00 3840.00 2160.00\n"
                             "  a 0.00 0.00 100.00 2160.00\n"
                             "  b 120.00 0.00 140.00 2160.00\n",
                             fixture.dump( viewport._uiScale ).c_str() );
        }
    }
}

/** @brief [UiScaleTest] 글자 배율 1.5 → 글 위젯의 원하는 크기만 1.5 배, 고정 크기 위젯은 그대로(레이아웃이 다시 돈다) */
SW_TEST_CASE( UiScaleTest, TextScaleGrowsTextOnly )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::BoxPanel*             pRow   = fixture.setRoot<sw::BoxPanel>( "row" );
    sw::Widget*               pText  = pRow->addChild( sw::make_unique<sw::test::TestWrapWidget>( "text", 5 ) );
    sw::Widget*               pFixed = fixture.addFixed( pRow, "fixed", 30.0f, 30.0f );
    fixture.update();
    SW_EXPECT_NEAR_EQUAL( 50.0f, pText->getDesiredSize()._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pText->getDesiredSize()._y, 0.001f );

    {
        const ScopedFloatVariable textScale( "gv_uiTextScale", 1.5f );
        const sw::UiSystem        uiSystem;
        fixture.getContext()._textScale = uiSystem.makeLayoutContext()._textScale;
    }
    SW_EXPECT_NEAR_EQUAL( 1.5f, fixture.getContext()._textScale, 0.0001f );
    fixture.update();
    SW_EXPECT_NEAR_EQUAL( 75.0f, pText->getDesiredSize()._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pText->getDesiredSize()._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pFixed->getDesiredSize()._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 75.0f, pFixed->getGeometry()._position._x, 0.001f );
}

/** @brief [UiScaleTest] gv_uiDebugSafeZone 0.05 → 안전 영역 패널의 자식이 각 변 5 % 안쪽 */
SW_TEST_CASE( UiScaleTest, SafeZoneInsetsChild )
{
    sw::UiViewport viewport{};
    {
        const ScopedFloatVariable safeZone( "gv_uiDebugSafeZone", 0.05f );
        const sw::UiSystem        uiSystem;
        viewport = uiSystem.computeViewport( sw::float2{ 1920.0f, 1080.0f }, 1.0f );
    }
    sw::test::UiLayoutFixture fixture( 0.0f, 0.0f );
    fixture.getContext()._viewportSize = viewport._size;
    fixture.getContext()._safeInsets   = viewport._safeInsets;
    sw::SafeZonePanel* pSafe           = fixture.setRoot<sw::SafeZonePanel>( "safe" );
    fixture.addFixed( pSafe, "hud", 10.0f, 10.0f );
    fixture.update();
    SW_EXPECT_STREQ( "safe 0.00 0.00 1920.00 1080.00\n"
                     "  hud 96.00 54.00 1728.00 972.00\n",
                     fixture.dump().c_str() );

    // 안전 영역이 사라지면(같은 뷰포트 크기) 루트부터 다시 놓는다.
    fixture.getContext()._safeInsets = sw::float4{};
    fixture.update();
    SW_EXPECT_STREQ( "safe 0.00 0.00 1920.00 1080.00\n"
                     "  hud 0.00 0.00 1920.00 1080.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiScaleTest] 창 콘텐츠 배율은 기본으로 곱하지 않는다(해상도 규칙이 이미 따른다) — 설정이 켤 때만 */
SW_TEST_CASE( UiScaleTest, ContentScaleIgnoredByDefault )
{
    sw::UiScaleSettings settings{};
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::UiScaleUtil::makeViewport( settings, sw::float2{ 1920.0f, 1080.0f }, 1.0f, 1.5f, 0.0f )._uiScale, 0.0001f );
    settings._bApplyContentScale = true;
    SW_EXPECT_NEAR_EQUAL( 1.5f, sw::UiScaleUtil::makeViewport( settings, sw::float2{ 1920.0f, 1080.0f }, 1.0f, 1.5f, 0.0f )._uiScale, 0.0001f );
}

/** @brief [UiScaleTest] 엔진 기본 배율 규칙(engine/ui/uiscale.xml)이 읽히고 기본값과 같다 */
SW_TEST_CASE( UiScaleTest, EngineScaleSettingsLoad )
{
    sw::UiScaleSettings settings{};
    SW_ASSERT_TRUE( settings.loadFromResource( "engine/ui/uiscale.xml" ) );
    SW_EXPECT_TRUE( settings._rule == sw::UiScaleRule::ShortestSide );
    SW_EXPECT_NEAR_EQUAL( 1080.0f, settings._referenceHeight, 0.001f );
    SW_EXPECT_FALSE( settings._bApplyContentScale );
}
