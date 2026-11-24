// SceneStreamingSourcesTest — 원점마다 반경 · 우선순위(`StreamingSource`): 올리는 셀은 원점 반경의 합집합이고, 급한 우선순위가 덮는 셀을 먼저 짓고,
// 잘못된 반경은 거절되며, 셀 스트리밍 끝(`isCellStreamingCompleted`)은 조각 없는 셀을 기다리지 않는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

#include <limits>

using namespace sw;

namespace
{
    struct SceneStreamingSourcesTestInternal
    {
        static constexpr uint32  kWholeBudgetMicroseconds = 1'000'000'000u; ///< 한 프레임에 모두 짓는 예산
        static constexpr uint32  kTinyBudgetMicroseconds  = 1u;             ///< 프레임마다 엔티티 하나
        static constexpr int32   kCellCount               = 6;
        static constexpr float32 kCellSize                = 10.0f;

        /** @brief 셀 (0,0) ~ (5,0) 에 엔티티 하나씩(파일 id 1 부터, 셀 가운데)인 스트리밍 문서입니다. 셀 한 변 10 m, 격자 로딩 반경 6 m, 여유 0. */
        static SceneDocument makeDocument( uint32 budgetMicroseconds )
        {
            SceneDocument doc{};
            doc._name                                        = "StreamedSources";
            doc._sceneSettings._bStreaming                   = true;
            doc._sceneSettings._cellGrid._cellSize           = kCellSize;
            doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
            doc._sceneSettings._unloadMargin                 = 0.0f;
            doc._sceneSettings._maxCellLoadsPerFrame         = 8;
            doc._sceneSettings._addToWorldBudgetMicroseconds = budgetMicroseconds;
            doc._bValid                                      = true;
            for ( int32 x = 0; x < kCellCount; ++x )
            {
                SceneDocument::StreamingCell cell{};
                cell._cell = StreamingCellCoord{ x, 0 };
                SceneDocument::SceneObjectNode node{};
                node._fileID = static_cast<uint64>( x + 1 );
                StringBuilder<constant::buffer::kMax512> text;
                text.appendFormat( "Crate%#", static_cast<int64>( x ) );
                node._name = string( text.view() );
                text.clear();
                text.appendFormat( "<GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"%#,0,5\" /></_listComponent></GameObject>", node._name,
                                   static_cast<int64>( x * 10 + 5 ) );
                node._embeddedXML = string( text.view() );
                cell._listSceneObjectNode.push_back( std::move( node ) );
                doc._listStreamingCell.push_back( std::move( cell ) );
            }
            return doc;
        }

        /** @brief 셀 x 가 올라 있는지 비트로 모읍니다(비트 x). */
        static uint32 collectLoadedMask( const SceneStreamingSubsystem& streaming )
        {
            uint32 mask = 0;
            for ( int32 x = 0; x < kCellCount; ++x )
            {
                mask |= streaming.isCellLoaded( StreamingCellCoord{ x, 0 } ) ? ( 1u << x ) : 0u;
            }
            return mask;
        }
    };
    using Internal = SceneStreamingSourcesTestInternal;

    /** @brief 테스트 동안 결정적 실행의 "프레임마다 끝내기" 를 끈다 — 켜져 있으면 예산 없이 모두 짓는다. */
    class ScopedSourcesFreeRunningPolicy
    {
    public:
        ScopedSourcesFreeRunningPolicy()
            : _previous{ engine::getSubsystem<DeterministicRun>().getPolicy() }
        {
            DeterministicRunPolicy policy     = _previous;
            policy._bFinishAssetJobsEachFrame = SW_FALSE;
            engine::getSubsystem<DeterministicRun>().setPolicy( policy );
        }
        ~ScopedSourcesFreeRunningPolicy() { engine::getSubsystem<DeterministicRun>().setPolicy( _previous ); }
        ScopedSourcesFreeRunningPolicy( const ScopedSourcesFreeRunningPolicy& )            = delete;
        ScopedSourcesFreeRunningPolicy& operator=( const ScopedSourcesFreeRunningPolicy& ) = delete;

    private:
        DeterministicRunPolicy _previous;
    };
} // namespace

/** @brief [SceneStreamingSourcesTest] 올린 셀은 원점마다 제 반경 안 셀의 합집합이다 — 반경 0 원점은 제 셀만, 반경 12 원점은 이웃 셀까지. 원점이 빠지면 그 셀만 내린다 */
SW_TEST_CASE( SceneStreamingSourcesTest, LoadedCellsAreUnionOfSourceRadii )
{
    Scene scene{ "Union" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kWholeBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();

    // A: 셀 0 가운데, 반경 0 → 셀 0. B: 셀 4 가운데, 반경 12 → 셀 3 · 4 · 5(셀 2 는 15 m 떨어졌다).
    const StreamingSource arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f },  0.0f, StreamingSourcePriority::Normal},
        StreamingSource{float3{ 45.0f, 0.0f, 5.0f }, 12.0f, StreamingSourcePriority::Normal}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( ( 1u << 0 ) | ( 1u << 3 ) | ( 1u << 4 ) | ( 1u << 5 ), Internal::collectLoadedMask( streaming ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( streaming.getActiveSources().size() ) );

    // B 만 남으면 A 의 셀 0 만 내린다.
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( &arrSource[1], 1 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( ( 1u << 3 ) | ( 1u << 4 ) | ( 1u << 5 ), Internal::collectLoadedMask( streaming ) );
}

/** @brief [SceneStreamingSourcesTest] 반경이 NaN · 음수 · 무한인 원점 목록은 오류로 거절되고 지난 원점이 그대로 남는다 */
SW_TEST_CASE( SceneStreamingSourcesTest, InvalidSourceRadiusIsRefused )
{
    SW_EXPECT_LOG( "SceneStreaming.SourceRadiusInvalid" );
    Scene scene{ "Refused" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kWholeBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    const StreamingSource    valid{
        float3{ 5.0f, 0.0f, 5.0f },
        0.0f, StreamingSourcePriority::Normal
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( &valid, 1 ) ) );

    const float32 arrBadRadius[] = { std::numeric_limits<float32>::quiet_NaN(), -1.0f, std::numeric_limits<float32>::infinity() };
    for ( const float32 badRadius : arrBadRadius )
    {
        const StreamingSource arrSource[] = {
            StreamingSource{float3{ 45.0f, 0.0f, 5.0f },     12.0f, StreamingSourcePriority::Normal},
            StreamingSource{float3{ 25.0f, 0.0f, 5.0f }, badRadius, StreamingSourcePriority::Normal}
        };
        SW_EXPECT_FALSE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    }
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 1u << 0, Internal::collectLoadedMask( streaming ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( streaming.getActiveSources().size() ) );
    SW_EXPECT_EQUAL( 5.0f, streaming.getActiveSources()[0]._position._x );
}

/** @brief [SceneStreamingSourcesTest] 시간 예산이 엔티티 하나뿐이면 급한 우선순위 원점이 덮는 셀을 먼저 짓는다 — 거리가 더 멀어도 */
SW_TEST_CASE( SceneStreamingSourcesTest, UrgentSourceCellsBuildFirst )
{
    const ScopedSourcesFreeRunningPolicy policy;
    Scene                                scene{ "Priority" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kTinyBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();

    // 셀 0 은 Low 원점 바로 위(거리 0), 셀 5 는 Highest 원점 바로 위(거리 0) — 같은 거리면 셀 순서로 0 이 먼저였다.
    const StreamingSource arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f }, 0.0f,     StreamingSourcePriority::Low},
        StreamingSource{float3{ 55.0f, 0.0f, 5.0f }, 0.0f, StreamingSourcePriority::Highest}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 1u << 5, Internal::collectLoadedMask( streaming ) );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( ( 1u << 0 ) | ( 1u << 5 ), Internal::collectLoadedMask( streaming ) );
}

/** @brief [SceneStreamingSourcesTest] 셀 스트리밍 끝은 조각 없는 셀 · 오른 셀이면 true, 원점 밖이라 내려간 셀이면 false 다 */
SW_TEST_CASE( SceneStreamingSourcesTest, CellStreamingCompletedSkipsEmptyCells )
{
    Scene scene{ "Completed" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kWholeBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    const StreamingSource    source{
        float3{ 5.0f, 0.0f, 5.0f },
        0.0f, StreamingSourcePriority::Normal
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_TRUE( streaming.isCellStreamingCompleted( StreamingCellCoord{ 0, 0 } ) );   // 올랐다
    SW_EXPECT_FALSE( streaming.isCellStreamingCompleted( StreamingCellCoord{ 3, 0 } ) );  // 조각이 있고 원점 밖이다
    SW_EXPECT_TRUE( streaming.isCellStreamingCompleted( StreamingCellCoord{ 40, 40 } ) ); // 조각이 없다 — 기다릴 것이 없다
}

/**
 * @brief [SceneStreamingSourcesTest] 거대 · NaN 좌표와 거대 반경이 정의된 동작으로 묶인다 — 셀 번호는 ±2^30 에서 포화하고, NaN 위치 원점은 거절되며,
 *        거대 반경은 받은 셀만 훑어 모두 올리고 거대 위치는 아무 셀도 올리지 않는다
 */
SW_TEST_CASE( SceneStreamingSourcesTest, HugeOrNaNCoordinatesStayBounded )
{
    SW_EXPECT_LOG( "SceneStreaming.SourcePositionInvalid" );
    constexpr int32      kSaturatedCell = 1 << 30; // `StreamingCellGrid::computeCell` 의 포화 한계
    constexpr float32    kHuge          = 1.0e30f;
    StreamingCellGridDef def;
    def._cellSize      = Internal::kCellSize;
    def._loadingRadius = 6.0f;
    StreamingCellGrid grid;
    SW_REQUIRE_TRUE( StreamingCellGrid::tryCreate( def, grid ) );
    const StreamingCellCoord hugeCell = grid.computeCell( float3{ kHuge, 0.0f, -kHuge } );
    SW_EXPECT_EQUAL( kSaturatedCell, hugeCell._x );
    SW_EXPECT_EQUAL( -kSaturatedCell, hugeCell._z );
    SW_EXPECT_EQUAL( -kSaturatedCell, grid.computeCell( float3{ std::numeric_limits<float32>::quiet_NaN(), 0.0f, 0.0f } )._x );

    Scene scene{ "Bounded" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kWholeBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    const StreamingSource    nanSource{
        float3{ std::numeric_limits<float32>::quiet_NaN(), 0.0f, 5.0f },
        6.0f, StreamingSourcePriority::Normal
    };
    SW_EXPECT_FALSE( streaming.setExternalSources( span<const StreamingSource>( &nanSource, 1 ) ) );

    // 거대 반경: 범위 셀은 수천억 개지만 받은 셀 여섯만 훑는다.
    const StreamingSource wideSource{
        float3{ 5.0f, 0.0f, 5.0f },
        kHuge, StreamingSourcePriority::Normal
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( &wideSource, 1 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( ( 1u << Internal::kCellCount ) - 1u, Internal::collectLoadedMask( streaming ) );

    // 거대 위치: 포화한 셀 하나만 보고 아무 셀도 덮지 않는다 — 올라 있던 셀은 모두 내린다.
    const StreamingSource farSource{
        float3{ kHuge, 0.0f, kHuge },
        6.0f, StreamingSourcePriority::Normal
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( &farSource, 1 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, Internal::collectLoadedMask( streaming ) );
}

/**
 * @brief [SceneStreamingSourcesTest] 조각은 셀 사각형이 아니라 내용 경계로 오르고 내린다 — 피벗 셀이 반경 밖이어도 몸이 반경 안이면 올라 있고, 그 몸이 덮는 자리는 내려가 있으면 바닥이 없다
 */
SW_TEST_CASE( SceneStreamingSourcesTest, ContentBoundsDecideLoading )
{
    SceneDocument doc{};
    doc._name                                        = "LargeBody";
    doc._sceneSettings._bStreaming                   = true;
    doc._sceneSettings._cellGrid._cellSize           = 10.0f;
    doc._sceneSettings._cellGrid._loadingRadius      = 20.0f;
    doc._sceneSettings._unloadMargin                 = 0.0f;
    doc._sceneSettings._maxCellLoadsPerFrame         = 4;
    doc._sceneSettings._addToWorldBudgetMicroseconds = 1'000'000'000u;
    doc._bValid                                      = true;
    SceneDocument::StreamingCell cell{};
    cell._cell = StreamingCellCoord{ 0, 0 };
    SceneDocument::SceneObjectNode node{};
    node._fileID      = 1;
    node._name        = "Bridge";
    node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Bridge\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"5,0,5\" /></_listComponent></GameObject>";
    cell._listSceneObjectNode.push_back( node );
    cell._contentBounds = StreamingCellBounds{ 2.0f, 4.0f, 45.0f, 6.0f }; // 피벗 셀 (0,0) 에서 X 45 까지 뻗은 다리
    doc._listStreamingCell.push_back( std::move( cell ) );

    Scene scene{ "LargeBody" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    const auto               moveTo    = [&streaming]( float32 x )
    {
        const StreamingSource source{
            float3{ x, 0.0f, 5.0f },
            20.0f, StreamingSourcePriority::Normal
        };
        SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        streaming.update( 0.0f );
    };
    moveTo( 70.0f ); // 다리 끝에서 25 m — 반경 밖
    SW_EXPECT_FALSE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_TRUE( streaming.isFloorMissing( float3{ 30.0f, 1.0f, 5.0f } ) );          // 다리가 덮는 자리
    SW_EXPECT_FALSE( streaming.isFloorMissing( float3{ 30.0f, 1.0f, 15.0f } ) );        // 다리 옆 — 조각 경계는 셀 사각형(Z 0 ~ 10)에 다리를 더한 것이다
    SW_EXPECT_TRUE( streaming.isCellStreamingCompleted( StreamingCellCoord{ 3, 0 } ) ); // 그 좌표의 조각은 없다 — 복제 보류는 기다리지 않는다
    moveTo( 60.0f );                                                                    // 셀 (0,0) 은 50 m 지만 다리 끝은 15 m 다
    SW_EXPECT_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( streaming.isFloorMissing( float3{ 30.0f, 1.0f, 5.0f } ) );
    moveTo( 70.0f );
    SW_EXPECT_FALSE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
}
