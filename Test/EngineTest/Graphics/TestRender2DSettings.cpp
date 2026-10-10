#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

// Render2DSettingsTest — 프로젝트 2D 렌더 설정(render2d.xml): 정렬 레이어 표 · 정렬 키 · 투명 정렬 축. 디바이스 없음(nogpu).

namespace
{
    constexpr const utf8* kLayeredXML = "<Render2DSettings>"
                                        "  <TransparencySort mode=\"CustomAxis\" axis=\"0 2 0\"/>"
                                        "  <SortingLayers>"
                                        "    <Layer name=\"Background\"/><Layer name=\"Default\"/><Layer name=\"WorldUI\"/>"
                                        "  </SortingLayers>"
                                        "</Render2DSettings>";
} // namespace

/**
 * @brief [Render2DSettingsTest] 표의 줄 순서가 그리는 순서이고, 키는 레이어가 먼저 · 레이어 안 순서가 다음이다
 * @details 레이어가 다르면 순서 값이 아무리 커도 아래 레이어가 먼저다(유니티 Sorting Layer 가 Order in Layer 를 이긴다). 순서는 음수도 되고
 *          범위 밖은 묶인다 — 묶지 않으면 순서 +40000 이 다음 레이어의 키로 넘쳐 들어간다.
 */
SW_TEST_CASE( Render2DSettingsTest, LayerOrderWinsOverOrderInLayer )
{
    sw::Render2DSettings settings;
    SW_ASSERT_TRUE( settings.loadFromXMLText( kLayeredXML, "<test>" ) );
    SW_ASSERT_EQUAL( 3u, settings.getSortingLayerCount() );
    SW_EXPECT_EQUAL( 1u, settings.getDefaultSortingLayer() );
    SW_EXPECT_EQUAL( 2, settings.findSortingLayer( sw::hashed_string( "worldui" ) ) ); // 이름은 대소문자를 가리지 않는다
    SW_EXPECT_EQUAL( -1, settings.findSortingLayer( sw::hashed_string( "Sky" ) ) );

    const uint32 backgroundTop = sw::Render2DSettings::makeSortKey( 0, 32767 );
    const uint32 defaultBottom = sw::Render2DSettings::makeSortKey( 1, -32767 );
    const uint32 defaultZero   = sw::Render2DSettings::makeSortKey( 1, 0 );
    const uint32 defaultOne    = sw::Render2DSettings::makeSortKey( 1, 1 );
    const uint32 worldUiBottom = sw::Render2DSettings::makeSortKey( 2, -32767 );
    SW_EXPECT_TRUE( backgroundTop < defaultBottom );
    SW_EXPECT_TRUE( defaultBottom < defaultZero );
    SW_EXPECT_TRUE( defaultZero < defaultOne );
    SW_EXPECT_TRUE( defaultOne < worldUiBottom );
    SW_EXPECT_EQUAL( defaultZero, settings.getDefaultSortKey() );
    // 범위 밖 순서는 묶인다 — 다음 레이어로 넘치지 않는다.
    SW_EXPECT_EQUAL( sw::Render2DSettings::makeSortKey( 1, 32767 ), sw::Render2DSettings::makeSortKey( 1, 40000 ) );
    SW_EXPECT_TRUE( sw::Render2DSettings::makeSortKey( 1, 40000 ) < worldUiBottom );
    // 자리표 키(0)는 어떤 유효한 키와도 겹치지 않는다.
    SW_EXPECT_NOT_EQUAL( sw::Render2DSettings::kDefaultSortKeyPlaceholder, sw::Render2DSettings::makeSortKey( 0, -40000 ) );

    uint32 sortKey = 0;
    SW_EXPECT_TRUE( settings.resolveSortKey( sw::hashed_string( "WorldUI" ), 3, sortKey ) );
    SW_EXPECT_EQUAL( sw::Render2DSettings::makeSortKey( 2, 3 ), sortKey );
    // 모르는 이름은 false 이고 기본 레이어 키다(부르는 쪽이 누가 틀렸는지 알린다).
    SW_EXPECT_FALSE( settings.resolveSortKey( sw::hashed_string( "Sky" ), 3, sortKey ) );
    SW_EXPECT_EQUAL( sw::Render2DSettings::makeSortKey( 1, 3 ), sortKey );
}

/**
 * @brief [Render2DSettingsTest] 투명 정렬 축 — Auto 는 직교에서만 시선 축, CustomAxis 는 데이터의 축(정규화)
 * @details 직교 2D 카메라에서 거리로 재면 같은 Z 의 두 스프라이트가 카메라 XY 에 따라 앞뒤가 바뀐다. 영벡터는 "거리" 를 뜻한다.
 */
SW_TEST_CASE( Render2DSettingsTest, SortAxisFollowsModeAndProjection )
{
    const sw::float3 forward{ 0.0f, 0.0f, 1.0f };

    sw::Render2DSettings autoSettings;
    SW_EXPECT_TRUE( autoSettings.getTransparencySortMode() == sw::TransparencySortMode::Auto );
    SW_EXPECT_NEAR_EQUAL( 1.0f, autoSettings.computeTransparentSortAxis( true, forward )._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, autoSettings.computeTransparentSortAxis( false, forward ).getLengthSquared(), 1e-6f );

    sw::Render2DSettings custom;
    SW_ASSERT_TRUE( custom.loadFromXMLText( kLayeredXML, "<test>" ) );
    SW_EXPECT_TRUE( custom.getTransparencySortMode() == sw::TransparencySortMode::CustomAxis );
    const sw::float3 axis = custom.computeTransparentSortAxis( false, forward );
    SW_EXPECT_NEAR_EQUAL( 0.0f, axis._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, axis._y, 1e-6f ); // "0 2 0" 이 정규화된다
    SW_EXPECT_NEAR_EQUAL( 0.0f, axis._z, 1e-6f );
}

/**
 * @brief [Render2DSettingsTest] 표의 오류는 읽기 오류다 — 모르는 요소 · 속성 · 열거자, 같은 이름 둘, Default 없음, 영벡터 축
 * @details 실패한 읽기는 표를 바꾸지 않는다(앞서 읽은 표가 그대로 남는다).
 */
SW_TEST_CASE( Render2DSettingsTest, MalformedTablesAreLoadErrors )
{
    SW_TEST_DEFENSIVE_SCOPE( "each malformed table logs its load error" );
    sw::Render2DSettings settings;
    SW_ASSERT_TRUE( settings.loadFromXMLText( kLayeredXML, "<test>" ) );

    const utf8* arrBadXML[] = {
        "<Render2DSettings><TransparencySort mode=\"Isometric\"/></Render2DSettings>",
        "<Render2DSettings><TransparencySort mode=\"Auto\" axes=\"0 0 1\"/></Render2DSettings>",
        "<Render2DSettings><TransparencySort mode=\"CustomAxis\" axis=\"0 0 0\"/></Render2DSettings>",
        "<Render2DSettings><SortingLayers><Layer name=\"Default\"/><Layer name=\"default\"/></SortingLayers></Render2DSettings>",
        "<Render2DSettings><SortingLayers><Layer name=\"Background\"/></SortingLayers></Render2DSettings>",
        "<Render2DSettings><SortingLayers><Layer name=\"Default\" order=\"3\"/></SortingLayers></Render2DSettings>",
        "<Render2DSettings><SortingLayers><Group name=\"Default\"/></SortingLayers></Render2DSettings>",
        "<Render2DSettings><Layers/></Render2DSettings>",
        "<SortingLayers/>",
    };
    for ( const utf8* pXML : arrBadXML )
    {
        SW_EXPECT_FALSE_MSG( settings.loadFromXMLText( pXML, "<bad>" ), pXML );
        SW_EXPECT_EQUAL( 3u, settings.getSortingLayerCount() );
    }
}

/**
 * @brief [Render2DSettingsTest] 엔진 기본 표(engine/data/render2d.xml)가 읽히고 월드 UI 레이어가 Default 위에 있다
 * @details HP 바 · 데미지 숫자(`WorldUI`)는 같은 Z 의 스프라이트보다 늘 위다 — 표에서 그 줄이 Default 뒤에 와야 한다.
 */
SW_TEST_CASE( Render2DSettingsTest, EngineTableHasWorldUiAboveDefault )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::Render2DSettings settings;
    SW_ASSERT_TRUE( settings.loadFromResource( sw::Render2DSettings::getEngineSettingsPath() ) );
    const int32 worldUi = settings.findSortingLayer( sw::hashed_string( "WorldUI" ) );
    SW_ASSERT_TRUE( worldUi >= 0 );
    SW_EXPECT_TRUE( static_cast<uint32>( worldUi ) > settings.getDefaultSortingLayer() );
}
