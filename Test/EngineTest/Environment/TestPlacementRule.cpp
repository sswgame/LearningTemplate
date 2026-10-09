#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Placement/PlacementRule.h"
#include "Engine/Environment/Placement/PlacementTileSurface.h"

#include "TestFramework/TestFramework.h"

// 규칙 기반 배치(2D · 3D 공용 코어) — 결정성 · 필터 · 최소 거리 · 제외 영역 · 밀도 레이어 · 2D 타일 표면.

using namespace sw;

namespace
{
    /** @brief 시험용 해석 표면 — x < 50 은 기울기 0.5(경사 약 26.6 도), 그 밖은 평평하다. 높이는 x × 0.5 (x < 50) 또는 25. 레이어 1 은 z > 50 이다. */
    class AnalyticSlopeSurface final : public IPlacementSurface
    {
    public:
        AnalyticSlopeSurface() = default;

        bool sampleSurface( const float2& planePosition, PlacementSurfaceSample& outSample ) const override
        {
            outSample                 = PlacementSurfaceSample{};
            const bool bSloped        = planePosition._x < 50.0f;
            outSample._height         = bSloped ? planePosition._x * 0.5f : 25.0f;
            outSample._normal         = bSloped ? float3{ -0.5f, 1.0f, 0.0f }.normalize() : float3{ 0.0f, 1.0f, 0.0f };
            const float32 upperWeight = planePosition._y > 50.0f ? 1.0f : 0.0f;
            outSample._layerWeight    = float4{ 1.0f - upperWeight, upperWeight, 0.0f, 0.0f };
            return true;
        }
    };

    struct PlacementRuleTestUtil
    {
        static PlacementRule makeRule()
        {
            PlacementRule rule;
            rule._density     = 0.5f;
            rule._minDistance = 0.0f;
            rule._scaleMin    = 0.8f;
            rule._scaleMax    = 1.2f;
            rule._seed        = 1234u;
            return rule;
        }

        static PlacementRegion makeRegion( float32 size )
        {
            PlacementRegion region;
            region._min = float2{ 0.0f, 0.0f };
            region._max = float2{ size, size };
            return region;
        }

        static bool isSameInstance( const PlacementInstance& lhs, const PlacementInstance& rhs )
        {
            return lhs._planePosition._x == rhs._planePosition._x && lhs._planePosition._y == rhs._planePosition._y && lhs._height == rhs._height &&
                   lhs._scale == rhs._scale && lhs._yaw == rhs._yaw && lhs._entryIndex == rhs._entryIndex && lhs._hash == rhs._hash;
        }
    };
} // namespace

/**
 * @brief [PlacementRuleTest] 같은 규칙 · 씨앗이면 인스턴스가 비트까지 같고, 씨앗이 다르면 다르다 · 크기 · 요는 범위 안이다
 */
SW_TEST_CASE( PlacementRuleTest, SameSeedGivesTheSameInstances )
{
    const PlacementRule       rule   = PlacementRuleTestUtil::makeRule();
    const PlacementRegion     region = PlacementRuleTestUtil::makeRegion( 40.0f );
    const vector<float32>     listWeight{ 1.0f, 2.0f };
    vector<PlacementInstance> listFirst;
    vector<PlacementInstance> listSecond;
    PlacementScatter::scatter( rule, region, nullptr, {}, listWeight, listFirst );
    PlacementScatter::scatter( rule, region, nullptr, {}, listWeight, listSecond );
    SW_ASSERT_EQUAL( static_cast<size_t>( 800 ), listFirst.size() ); // 밀도 0.5 × 40 × 40, 필터 없음
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    bool   bSame    = true;
    bool   bInRange = true;
    uint32 entryCount[2]{};
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        const PlacementInstance& instance = listFirst[index];
        bSame                             = bSame && PlacementRuleTestUtil::isSameInstance( instance, listSecond[index] );
        bInRange                          = bInRange && 0.8f <= instance._scale && instance._scale <= 1.2f && 0.0f <= instance._yaw && instance._yaw <= 6.2831853f;
        bInRange                          = bInRange && 0.0f <= instance._planePosition._x && instance._planePosition._x <= 40.0f && 0.0f <= instance._planePosition._y && instance._planePosition._y <= 40.0f;
        ++entryCount[instance._entryIndex];
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bInRange );
    // 비중 1 : 2 — 넉넉한 표본이라 두 번째가 첫째보다 많다.
    SW_EXPECT_TRUE( entryCount[1] > entryCount[0] );

    PlacementRule otherRule = rule;
    otherRule._seed         = 4321u;
    vector<PlacementInstance> listOther;
    PlacementScatter::scatter( otherRule, region, nullptr, {}, listWeight, listOther );
    SW_ASSERT_EQUAL( listFirst.size(), listOther.size() );
    SW_EXPECT_FALSE( PlacementRuleTestUtil::isSameInstance( listFirst[0], listOther[0] ) );
}

/**
 * @brief [PlacementRuleTest] 최소 거리(푸아송)를 켜면 어느 두 인스턴스도 그보다 가깝지 않고, 끄면 가까운 쌍이 있다
 */
SW_TEST_CASE( PlacementRuleTest, PoissonMinimumDistanceHolds )
{
    PlacementRule rule = PlacementRuleTestUtil::makeRule();
    rule._density      = 2.0f;
    rule._minDistance  = 1.5f;
    vector<PlacementInstance> listInstance;
    PlacementScatter::scatter( rule, PlacementRuleTestUtil::makeRegion( 30.0f ), nullptr, {}, {}, listInstance );
    SW_ASSERT_TRUE( listInstance.size() > 100 );
    float32 closest = MathUtil::kMaxFloat;
    for ( size_t first = 0; first < listInstance.size(); ++first )
    {
        for ( size_t second = first + 1; second < listInstance.size(); ++second )
        {
            closest = MathUtil::min( closest, ( listInstance[first]._planePosition - listInstance[second]._planePosition ).getLength() );
        }
    }
    SW_EXPECT_TRUE( closest >= 1.5f );

    rule._minDistance = 0.0f;
    PlacementScatter::scatter( rule, PlacementRuleTestUtil::makeRegion( 30.0f ), nullptr, {}, {}, listInstance );
    float32 closestFree = MathUtil::kMaxFloat;
    for ( size_t first = 0; first < listInstance.size(); ++first )
    {
        for ( size_t second = first + 1; second < listInstance.size(); ++second )
        {
            closestFree = MathUtil::min( closestFree, ( listInstance[first]._planePosition - listInstance[second]._planePosition ).getLength() );
        }
    }
    SW_EXPECT_TRUE( closestFree < 1.5f );
}

/**
 * @brief [PlacementRuleTest] 경사 · 높이 필터는 표면 값으로 거른다 — 26.6 도 비탈은 20 도 상한에 걸리고, 높이 범위 밖에는 없다
 */
SW_TEST_CASE( PlacementRuleTest, SlopeAndHeightFiltersAreRespected )
{
    const AnalyticSlopeSurface surface;
    PlacementRule              rule = PlacementRuleTestUtil::makeRule();
    rule._slopeMax                  = MathUtil::toRadian( 20.0f );
    vector<PlacementInstance> listInstance;
    PlacementScatter::scatter( rule, PlacementRuleTestUtil::makeRegion( 100.0f ), &surface, {}, {}, listInstance );
    SW_ASSERT_TRUE( listInstance.empty() == false );
    bool bFlatOnly = true;
    for ( const PlacementInstance& instance : listInstance )
    {
        bFlatOnly = bFlatOnly && instance._planePosition._x >= 50.0f && PlacementScatter::computeSlope( instance._normal ) <= rule._slopeMax;
    }
    SW_EXPECT_TRUE( bFlatOnly );

    // 비탈만 — 경사 하한 20 도, 높이 5..15 m(x 10..30).
    rule._slopeMax  = MathUtil::toRadian( 90.0f );
    rule._slopeMin  = MathUtil::toRadian( 20.0f );
    rule._heightMin = 5.0f;
    rule._heightMax = 15.0f;
    PlacementScatter::scatter( rule, PlacementRuleTestUtil::makeRegion( 100.0f ), &surface, {}, {}, listInstance );
    SW_ASSERT_TRUE( listInstance.empty() == false );
    bool bInBand = true;
    for ( const PlacementInstance& instance : listInstance )
    {
        bInBand = bInBand && 5.0f <= instance._height && instance._height <= 15.0f;
        bInBand = bInBand && 10.0f <= instance._planePosition._x && instance._planePosition._x <= 30.0f;
    }
    SW_EXPECT_TRUE( bInBand );
}

/**
 * @brief [PlacementRuleTest] 레이어 필터와 밀도 레이어 — 레이어 1(z > 50)에만 놓고, 밀도 레이어가 0 인 곳은 비운다
 */
SW_TEST_CASE( PlacementRuleTest, LayerFilterAndDensityMap )
{
    const AnalyticSlopeSurface surface;
    PlacementRule              rule = PlacementRuleTestUtil::makeRule();
    rule._layerIndex                = 1;
    rule._layerMinWeight            = 0.5f;
    vector<PlacementInstance> listInstance;
    PlacementScatter::scatter( rule, PlacementRuleTestUtil::makeRegion( 100.0f ), &surface, {}, {}, listInstance );
    SW_ASSERT_TRUE( listInstance.empty() == false );
    bool bUpperOnly = true;
    for ( const PlacementInstance& instance : listInstance )
    {
        bUpperOnly = bUpperOnly && instance._planePosition._y > 50.0f;
    }
    SW_EXPECT_TRUE( bUpperOnly );

    PlacementRule densityRule      = PlacementRuleTestUtil::makeRule();
    densityRule._densityLayerIndex = 1; // 가중치 0 → 확률 0, 1 → 확률 1
    vector<PlacementInstance> listDense;
    PlacementScatter::scatter( densityRule, PlacementRuleTestUtil::makeRegion( 100.0f ), &surface, {}, {}, listDense );
    SW_EXPECT_EQUAL( listInstance.size(), listDense.size() );
}

/**
 * @brief [PlacementRuleTest] 제외 영역(원 · 사각형) 안에는 없고, 영역 밖의 인스턴스는 제외가 없을 때와 같다(제외가 다른 후보의 난수를 밀지 않는다)
 */
SW_TEST_CASE( PlacementRuleTest, ExclusionAreasStayEmptyAndKeepTheRest )
{
    const PlacementRule       rule   = PlacementRuleTestUtil::makeRule();
    const PlacementRegion     region = PlacementRuleTestUtil::makeRegion( 40.0f );
    vector<PlacementInstance> listOpen;
    PlacementScatter::scatter( rule, region, nullptr, {}, {}, listOpen );

    PlacementExclusion circle;
    circle._shape  = PlacementExclusionShape::Circle;
    circle._center = float2{ 10.0f, 10.0f };
    circle._radius = 6.0f;
    PlacementExclusion rect;
    rect._shape      = PlacementExclusionShape::Rect;
    rect._center     = float2{ 30.0f, 25.0f };
    rect._halfExtent = float2{ 4.0f, 10.0f };
    const vector<PlacementExclusion> listExclusion{ circle, rect };
    vector<PlacementInstance>        listExcluded;
    PlacementScatter::scatter( rule, region, nullptr, listExclusion, {}, listExcluded );
    SW_ASSERT_TRUE( listExcluded.size() < listOpen.size() );

    size_t keptCount = 0;
    bool   bOutside  = true;
    for ( const PlacementInstance& instance : listOpen )
    {
        if ( PlacementScatter::isExcluded( listExclusion, instance._planePosition ) )
            continue;
        ++keptCount;
    }
    for ( const PlacementInstance& instance : listExcluded )
    {
        bOutside = bOutside && PlacementScatter::isExcluded( listExclusion, instance._planePosition ) == false;
    }
    SW_EXPECT_TRUE( bOutside );
    SW_EXPECT_EQUAL( keptCount, listExcluded.size() );
}

/**
 * @brief [PlacementRuleTest] 2D — 같은 코어가 타일 맵 위에 흩뿌린다: 풀 타일(0)에만, 벽(막힘)과 맵 밖에는 없고, 최소 거리를 지키며 결정적이다
 */
SW_TEST_CASE( PlacementRuleTest, ScatterOnTwoDimensionalTiles )
{
    // 8 × 4 타일(한 변 2): 왼쪽 반은 풀(0), 오른쪽 반은 모래(1), 가운데 한 줄은 벽.
    vector<uint8> listTile( 8 * 4, 0u );
    for ( uint32 tileY = 0; tileY < 4; ++tileY )
    {
        for ( uint32 tileX = 4; tileX < 8; ++tileX )
        {
            listTile[tileY * 8 + tileX] = 1u;
        }
        listTile[tileY * 8 + 3] = PlacementTileSurface::kBlockedTile;
    }
    PlacementTileSurface surface;
    surface.setTiles( 8, 4, 2.0f, listTile );

    PlacementRule rule   = PlacementRuleTestUtil::makeRule();
    rule._density        = 3.0f;
    rule._minDistance    = 0.4f;
    rule._layerIndex     = 0;
    rule._layerMinWeight = 0.5f;
    PlacementRegion region;
    region._min = float2{ -4.0f, -4.0f }; // 맵 밖까지 덮는다 — 밖은 표면이 아니다
    region._max = float2{ 20.0f, 12.0f };
    vector<PlacementInstance> listInstance;
    PlacementScatter::scatter( rule, region, &surface, {}, {}, listInstance );
    SW_ASSERT_TRUE( listInstance.size() > 20 );
    bool    bOnGrass = true;
    float32 closest  = MathUtil::kMaxFloat;
    for ( size_t first = 0; first < listInstance.size(); ++first )
    {
        bOnGrass = bOnGrass && surface.getTileAt( listInstance[first]._planePosition ) == 0u && listInstance[first]._height == 0.0f;
        for ( size_t second = first + 1; second < listInstance.size(); ++second )
        {
            closest = MathUtil::min( closest, ( listInstance[first]._planePosition - listInstance[second]._planePosition ).getLength() );
        }
    }
    SW_EXPECT_TRUE( bOnGrass );
    SW_EXPECT_TRUE( closest >= 0.4f );

    vector<PlacementInstance> listAgain;
    PlacementScatter::scatter( rule, region, &surface, {}, {}, listAgain );
    SW_ASSERT_EQUAL( listInstance.size(), listAgain.size() );
    SW_EXPECT_TRUE( PlacementRuleTestUtil::isSameInstance( listInstance.back(), listAgain.back() ) );
}

/**
 * @brief [PlacementRuleTest] 월드 행렬 — 높이 · 요 · 크기를 싣고, 노멀 맞춤 1 이면 위쪽 축이 표면 노멀이다
 */
SW_TEST_CASE( PlacementRuleTest, WorldMatrixAlignsToTheNormal )
{
    PlacementInstance instance;
    instance._planePosition = float2{ 3.0f, -2.0f };
    instance._height        = 7.0f;
    instance._scale         = 2.0f;
    instance._yaw           = 0.0f;
    instance._normal        = float3{ 0.6f, 0.8f, 0.0f };
    instance._alignToNormal = 1.0f;
    const float4x4 world    = PlacementScatter::makeWorldMatrix( instance, float3{ 10.0f, 99.0f, 20.0f } );
    const float3   position = world.getTranslation();
    SW_EXPECT_NEAR_EQUAL( 13.0f, position._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, position._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 18.0f, position._z, 1.0e-5f );
    const float3 upAxis = float3{ world._21, world._22, world._23 } * 0.5f; // 크기 2 를 덜어 낸다
    SW_EXPECT_NEAR_EQUAL( 0.6f, upAxis._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, upAxis._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, upAxis._z, 1.0e-4f );
}
