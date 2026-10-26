// 기믹 회로 상태 벤치 — 쇼케이스 씬의 회로와 기믹 프리팹의 회로를 0 · 600 · 36000 스텝 돌린 뒤 고정 배치형 바이트 수 · 조각 수, `saveState` 의 시간 · 할당. 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"

#include <algorithm>

using namespace sw;

SW_TEST_LOAD_SUITE( GimmickStateBenchTest, "gimmick circuit state - fixed-layout bytes, chunk count and saveState cost after 0 / 600 / 36000 steps" );
SW_TEST_TIME_BUDGET( GimmickStateBenchTest, 20000, "Release 케이스 합 5.4 s(2026-10-20, 세 번 중 최대 아님 — 한 번 측정)의 네 배에 가깝게 — CI 러너 여유" );

namespace
{
    struct GimmickStateBenchInternal
    {
        static constexpr uint32 kSaveRounds     = 5;
        static constexpr uint32 kCallsPerSample = 2000;

        /** @brief 회로 하나의 고정 배치형 바이트 수와 조각 수, `saveState` 시간(중앙값 ns)과 할당 수(CRT 요청 · sw 할당자)를 찍습니다. */
        static void report( const utf8* pLabel, const utf8* pName, uint32 stepCount, const GimmickCircuitComponent& circuit )
        {
            BitWriter    writer;
            const bool   bWritten = FixedLayout::write( circuit.getState(), writer );
            const uint32 bytes    = bWritten ? static_cast<uint32>( writer.getByteCount() ) : 0;
            const uint32 chunks   = ( bytes + GimmickSnapshotUtil::kChunkPayloadBytes - 1 ) / GimmickSnapshotUtil::kChunkPayloadBytes;

            // 목록 용량을 이어 쓰므로 첫 호출이 용량을 만들고, 그 뒤 호출의 할당이 기대값 0 이다.
            // 시계 해상도(100 ns)보다 한 번이 짧아 표본 하나가 kCallsPerSample 번이고, 표본 kSaveRounds 개의 중앙값을 한 번당 ns 로 낸다.
            GimmickCircuitState scratch;
            circuit.getCircuit().saveState( scratch );
            vector<int64> listNano;
            listNano.reserve( kSaveRounds );
            const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
            uint64                crtBefore{ 0 };
            uint64                crtAfter{ 0 };
            uint64                bytesBefore{ 0 };
            const uint64          swBefore = pProfiler != nullptr ? pProfiler->getTotalAllocationCount() : 0;
            const bool            bCrt     = MemoryProfiler::getPlatformHeapTotals( bytesBefore, crtBefore );
            for ( uint32 round = 0; round < kSaveRounds; ++round )
            {
                const Stopwatch stopwatch;
                for ( uint32 call = 0; call < kCallsPerSample; ++call )
                {
                    circuit.getCircuit().saveState( scratch );
                }
                listNano.push_back( stopwatch.getElapsedNanoseconds() / kCallsPerSample );
            }
            const uint64 swAfter = pProfiler != nullptr ? pProfiler->getTotalAllocationCount() : 0;
            (void)MemoryProfiler::getPlatformHeapTotals( bytesBefore, crtAfter );
            std::sort( listNano.begin(), listNano.end() );
            SW_LOG_INFO( "[GimmickState] %# %# steps=%# nodes=%# bytes=%# chunks=%# (limit %# bytes) saveState median %# ns per call, allocations over %# calls (crt -1 = not measurable): crt %# sw %#",
                         pLabel, pName, stepCount, circuit.getState()._listNode.size(), bytes, chunks, GimmickSnapshotUtil::kMaxCircuitBytes,
                         listNano[listNano.size() / 2], kSaveRounds * kCallsPerSample, bCrt ? static_cast<int64>( crtAfter - crtBefore ) : int64( -1 ),
                         static_cast<int64>( swAfter - swBefore ) );
            (void)bWritten;
        }

        static void reportAll( const utf8* pLabel, GameObjectManager& manager, uint32 stepCount )
        {
            for ( GimmickCircuitComponent* pCircuit : manager.getComponentRegistry().getAll<GimmickCircuitComponent>() )
            {
                if ( pCircuit->getCircuit().isBuilt() == false || pCircuit->getOwner() == nullptr )
                    continue;
                report( pLabel, pCircuit->getOwner()->getName().c_str(), stepCount, *pCircuit );
            }
        }

        /** @brief 0 → 600 → 36000 스텝으로 진행하며 매 지점에서 찍습니다(누적 프레임). */
        static void runAndReport( const utf8* pLabel, GameObjectManager& manager )
        {
            manager.beginPlay();
            reportAll( pLabel, manager, 0 );
            test::updateFrames( manager, 600 );
            reportAll( pLabel, manager, 600 );
            test::updateFrames( manager, 36000 - 600 );
            reportAll( pLabel, manager, 36000 );
            manager.endPlay();
        }
    };
} // namespace

/** @brief [GimmickStateBenchTest] 쇼케이스 씬의 회로 */
SW_TEST_CASE( GimmickStateBenchTest, ShowcaseCircuits )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( "game/empty/maps/gimmickshowcase.scene.xml" ) );
    Scene scene{ "GimmickShowcase" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    GimmickStateBenchInternal::runAndReport( "scene", *scene.getObjectManager() );
}

/** @brief [GimmickStateBenchTest] 기믹 프리팹의 회로 */
SW_TEST_CASE( GimmickStateBenchTest, PrefabCircuits )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string   folder = FileUtil::joinPath( ResourcePaths::getRootFolderPath(), "common/prefabs/gimmicks" );
    vector<string> listFile;
    SW_ASSERT_TRUE( FileUtil::collectFiles( folder, ".xml", listFile, true ) );
    GameObjectManager manager;
    for ( const string& filePath : listFile )
    {
        PrefabAsset prefab;
        SW_ASSERT_TRUE( prefab.loadFromXMLFile( ResourcePaths::toResourceID( filePath ) ) );
        GameObject* pObject = manager.createGameObject( hashed_string( FileUtil::getFileNamePart( filePath ).c_str() ) );
        SW_ASSERT_TRUE( prefab.applyStateTo( pObject ) );
    }
    GimmickStateBenchInternal::runAndReport( "prefab", manager );
}
