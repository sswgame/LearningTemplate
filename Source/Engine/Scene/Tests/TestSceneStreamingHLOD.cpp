// SceneStreamingHLODTest — 씬 스트리밍 (라): 셀 조각의 HLOD 대리는 조각이 올라 있지 않을 때만 켜지고, 자리의 스트리밍 우선순위는 셀 읽기 차례와 같은 값이며,
// 로드 큐는 개발 도구가 본다(`DevSceneStreamingAccessTest`).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    struct SceneStreamingHLODTestInternal
    {
        static constexpr uint32  kWholeBudgetMicroseconds = 1'000'000'000u; ///< 한 프레임에 모두 짓는 예산
        static constexpr uint32  kTinyBudgetMicroseconds  = 1u;             ///< 프레임마다 엔티티 하나
        static constexpr int32   kCellCount               = 6;
        static constexpr uint64  kProxyFileIDBase         = 100; ///< 셀 x 의 대리 파일 id = 100 + x
        static constexpr float32 kCellSize                = 10.0f;

        /** @brief 셀 (0,0) ~ (5,0) 에 상자 하나씩(파일 id 1 부터)이고, 셀 0 · 2 에 HLOD 대리(늘 올라 있는 엔티티, 파일 id 100 · 102)가 있는 스트리밍 문서입니다. */
        static SceneDocument makeDocument( uint32 budgetMicroseconds )
        {
            SceneDocument doc{};
            doc._name                                        = "StreamedHLOD";
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
                if ( x == 0 || x == 2 )
                {
                    SceneDocument::SceneObjectNode proxy{};
                    proxy._fileID = kProxyFileIDBase + static_cast<uint64>( x );
                    text.clear();
                    text.appendFormat( "HLOD_%#_0", static_cast<int64>( x ) );
                    proxy._name = string( text.view() );
                    text.clear();
                    text.appendFormat( "<GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\" _bAlwaysLoaded=\"true\"><_listComponent><SceneComponent /></_listComponent></GameObject>", proxy._name );
                    proxy._embeddedXML = string( text.view() );
                    cell._hlodFileID   = proxy._fileID;
                    doc._listSceneObjectNode.push_back( std::move( proxy ) );
                }
                doc._listStreamingCell.push_back( std::move( cell ) );
            }
            return doc;
        }

        /** @brief 셀 x 의 HLOD 대리가 켜져 있으면 true 입니다(대리가 없으면 단언). */
        static bool isProxyActive( const Scene& scene, int32 x )
        {
            const uint64      objectID = scene.findObjectIDByFileID( kProxyFileIDBase + static_cast<uint64>( x ) );
            const GameObject* pProxy   = scene.getObjectManager()->findGameObjectByID( objectID );
            SW_EXPECT_NOT_NULL( pProxy );
            return pProxy != nullptr && pProxy->isActive();
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& position, StreamingSourcePriority priority )
        {
            const StreamingSource source{ position, 0.0f, priority };
            SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        }
    };
    using Internal = SceneStreamingHLODTestInternal;

    /** @brief 테스트 동안 결정적 실행의 "프레임마다 끝내기" 를 끈다 — 켜져 있으면 예산 없이 모두 짓는다. */
    class ScopedHLODFreeRunningPolicy
    {
    public:
        ScopedHLODFreeRunningPolicy()
            : _previous{ engine::getSubsystem<DeterministicRun>().getPolicy() }
        {
            DeterministicRunPolicy policy     = _previous;
            policy._bFinishAssetJobsEachFrame = SW_FALSE;
            engine::getSubsystem<DeterministicRun>().setPolicy( policy );
        }
        ~ScopedHLODFreeRunningPolicy() { engine::getSubsystem<DeterministicRun>().setPolicy( _previous ); }
        ScopedHLODFreeRunningPolicy( const ScopedHLODFreeRunningPolicy& )            = delete;
        ScopedHLODFreeRunningPolicy& operator=( const ScopedHLODFreeRunningPolicy& ) = delete;

    private:
        DeterministicRunPolicy _previous;
    };
} // namespace

/** @brief [SceneStreamingHLODTest] 대리는 조각이 올라 있지 않을 때만 켜진다 — 원점이 셀 0 이면 셀 0 대리가 꺼지고 셀 2 대리가 켜지며, 원점이 셀 2 로 가면 뒤바뀐다 */
SW_TEST_CASE( SceneStreamingHLODTest, ProxyShownOnlyWhileFragmentIsNotLoaded )
{
    Scene scene{ "HLOD" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kWholeBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();

    Internal::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f }, StreamingSourcePriority::Normal );
    streaming.update( 0.0f );
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( Internal::isProxyActive( scene, 0 ) );
    SW_EXPECT_TRUE( Internal::isProxyActive( scene, 2 ) );

    Internal::setSource( streaming, float3{ 25.0f, 0.0f, 5.0f }, StreamingSourcePriority::Normal );
    streaming.update( 0.0f ); // 같은 갱신에서 셀 0 을 내리고 셀 2 를 짓는다 — 대리도 같은 프레임에 맞춘다
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 2, 0 } ) );
    SW_EXPECT_FALSE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_TRUE( Internal::isProxyActive( scene, 0 ) );
    SW_EXPECT_FALSE( Internal::isProxyActive( scene, 2 ) );
}

/** @brief [SceneStreamingHLODTest] 다 짓기 전의 조각은 대리가 켜진 채다 — 예산이 엔티티 하나라 셀 0 이 지어지기 전에는 대리가 그린다(구멍이 나지 않는다) */
SW_TEST_CASE( SceneStreamingHLODTest, ProxyStaysUntilFragmentIsBuilt )
{
    const ScopedHLODFreeRunningPolicy policy;
    Scene                             scene{ "HLODBuild" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kTinyBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();

    // 셀 5 는 Highest 원점 위, 셀 0 은 Low 원점 위 — 첫 갱신은 셀 5 만 짓는다.
    const StreamingSource arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f }, 0.0f,     StreamingSourcePriority::Low},
        StreamingSource{float3{ 55.0f, 0.0f, 5.0f }, 0.0f, StreamingSourcePriority::Highest}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    streaming.update( 0.0f );
    SW_EXPECT_FALSE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_TRUE( Internal::isProxyActive( scene, 0 ) );
    streaming.update( 0.0f );
    SW_EXPECT_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( Internal::isProxyActive( scene, 0 ) );
}

/** @brief [SceneStreamingHLODTest] 자리의 우선순위는 그 셀을 덮는 원점 중 가장 급한 것이고(셀 읽기 차례와 같은 값) 덮는 원점이 없으면 `Lowest`, 스트리밍하지 않으면 `Normal` 이다 */
SW_TEST_CASE( SceneStreamingHLODTest, PriorityAtMatchesCellLoadPriority )
{
    Scene scene{ "HLODPriority" };
    SW_REQUIRE_TRUE( scene.instantiate( Internal::makeDocument( Internal::kWholeBudgetMicroseconds ) ) );
    SceneStreamingSubsystem& streaming   = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    const StreamingSource    arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f }, 0.0f,     StreamingSourcePriority::Low},
        StreamingSource{float3{ 55.0f, 0.0f, 5.0f }, 0.0f, StreamingSourcePriority::Highest}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    SW_EXPECT_TRUE( streaming.computePriorityAt( float3{ 3.0f, 1.0f, 7.0f } ) == StreamingSourcePriority::Low );
    SW_EXPECT_TRUE( streaming.computePriorityAt( float3{ 57.0f, 0.0f, 2.0f } ) == StreamingSourcePriority::Highest );
    SW_EXPECT_TRUE( streaming.computePriorityAt( float3{ 25.0f, 0.0f, 5.0f } ) == StreamingSourcePriority::Lowest );

    const Scene flat{ "HLODPlain" }; // 셀을 받지 않았다
    SW_EXPECT_TRUE( flat.getObjectManager()->getSubsystem<SceneStreamingSubsystem>().computePriorityAt( float3{} ) == StreamingSourcePriority::Normal );
}
