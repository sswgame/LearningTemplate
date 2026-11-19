// SceneStreamingSourcesTest — 원점마다 반경 · 우선순위(`StreamingSource`): 올리는 셀은 원점 반경의 합집합이고, 급한 우선순위가 덮는 셀을 먼저 짓고,
// 잘못된 반경은 거절되며, 셀 스트리밍 끝(`isCellStreamingCompleted`)은 조각 없는 셀을 기다리지 않는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

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
                node._fileID      = static_cast<uint64>( x + 1 );
                node._name        = "Crate" + sw::to_string( static_cast<int64>( x ) );
                node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"" + node._name + "\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"" +
                                    sw::to_string( static_cast<int64>( x * 10 + 5 ) ) + ",0,5\" /></_listComponent></GameObject>";
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
