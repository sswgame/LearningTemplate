#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/VFX/VFXCPUSimulation.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/VFX/VFXEffectDef.h"
#include "Engine/Visual/MeshInstanceBatch.h"

using namespace sw;

// VFXMeshSlotsTest — 입자의 인스턴스 칸(빈 칸 목록에서 꺼내고 죽으면 돌려준다)과 메시 방출기 배치에서 살아 있는 칸만 보이게 두는 표.

namespace
{
    struct VFXMeshSlotsTestUtil
    {
        static FloatCurve makeFlatCurve()
        {
            FloatCurve    curve;
            FloatCurveKey first;
            first._time  = 0.0f;
            first._value = 1.0f;
            FloatCurveKey last;
            last._time  = 1.0f;
            last._value = 1.0f;
            curve._listKey.push_back( first );
            curve._listKey.push_back( last );
            return curve;
        }

        /** @brief 단계(0.1 초)마다 입자 하나쯤을 만들고 입자가 0.35 초 사는 메시 방출기입니다 — 살아 있는 수는 넷 안팎이고 풀은 차지 않는다. */
        static VFXEmitterDef makeMeshEmitter()
        {
            VFXEmitterDef emitter;
            emitter._name                 = "Debris";
            emitter._renderMode           = VFXRenderMode::Mesh;
            emitter._mesh._meshID         = "Cube";
            emitter._maxParticles         = 64;
            emitter._duration             = 100.0f;
            emitter._spawnRate            = 10.0f;
            emitter._lifetimeMin          = 0.35f;
            emitter._lifetimeMax          = 0.35f;
            emitter._sizeOverLife         = makeFlatCurve();
            emitter._colorOverLife._red   = makeFlatCurve();
            emitter._colorOverLife._green = makeFlatCurve();
            emitter._colorOverLife._blue  = makeFlatCurve();
            emitter._colorOverLife._alpha = makeFlatCurve();
            return emitter;
        }

        /** @brief 칸마다 수명 비율입니다(`writeParticles` 와 `writeInstanceSlots` 가 같은 순서라 짝짓는다). */
        static unordered_map<uint32, float32> readLifeBySlot( const VFXCPUSimulation& simulation )
        {
            vector<GPUParticle> listParticle;
            vector<uint32>      listSlot;
            simulation.writeParticles( listParticle );
            simulation.writeInstanceSlots( listSlot );
            unordered_map<uint32, float32> lifeBySlot;
            for ( size_t index = 0; index < listSlot.size() && index < listParticle.size(); ++index )
                lifeBySlot[listSlot[index]] = listParticle[index]._rotationLife._y;
            return lifeBySlot;
        }

        /** @brief 배치에서 보이는 항목 번호입니다(오름차순). */
        static vector<uint32> readVisibleEntries( const MeshInstanceBatch& batch )
        {
            vector<uint32> listVisible;
            for ( uint32 entry = 0; entry < batch.getCount(); ++entry )
            {
                if ( batch.isEntryVisible( entry ) )
                    listVisible.push_back( entry );
            }
            return listVisible;
        }

        /** @brief 레지스트리의 더티 칸을 가져와 배치 안 항목 번호로 바꿉니다(오름차순, 표시는 지운다). */
        static vector<uint32> consumeDirtyEntries( PrimitiveRegistry& registry, const MeshInstanceBatch& batch )
        {
            vector<uint32> listState;
            vector<uint32> listTransform;
            registry.consumeDirty( listState, listTransform );
            listState.insert( listState.end(), listTransform.begin(), listTransform.end() );
            const uint32   firstSlot = static_cast<uint32>( registry.getAll().size() ) + batch.getFirstEntrySlot();
            vector<uint32> listEntry;
            for ( const uint32 slot : listState )
                listEntry.push_back( slot - firstSlot );
            VectorUtil::sort( listEntry );
            return listEntry;
        }
    };
    using Util = VFXMeshSlotsTestUtil;
} // namespace

/**
 * @brief [VFXMeshSlotsTest] 입자는 수명 내내 같은 칸을 쥐고, 죽은 칸은 다음 입자가 다시 쓴다 — 칸은 살아 있는 수 안팎에 머문다
 * @details 두 단계 모두에 있는 칸은 같은 입자이거나(수명 비율이 한 단계만큼 늘었다) 지난 입자가 죽고 새 입자가 받은 칸(비율이 처음 쪽으로 돌아갔다)이다.
 *          지우기(마지막과 바꾸기)로 풀 안의 자리가 섞여도 칸은 그대로여야 한다.
 */
SW_TEST_CASE( VFXMeshSlotsTest, ParticleKeepsItsSlotAndDeadSlotsAreReused )
{
    shared_ptr<const VFXEmitterDef> pDef = make_shared<const VFXEmitterDef>( Util::makeMeshEmitter() );
    VFXCPUSimulation                simulation{ pDef, 11u, 1.0f };
    constexpr float32               kStep     = 0.1f;
    const float32                   lifeDelta = kStep / pDef->_lifetimeMin;
    struct SlotState
    {
        float32 _life{ 0.0f };
        uint32  _stepIndex{ 0 };
    };
    unordered_map<uint32, SlotState> lastSeen; // 칸마다 마지막으로 본 수명 비율과 단계
    uint32                           maxSlot     = 0;
    uint32                           reusedCount = 0;
    for ( uint32 stepIndex = 1; stepIndex <= 40; ++stepIndex )
    {
        simulation.step( kStep, nullptr );
        vector<uint32> listSlot;
        simulation.writeInstanceSlots( listSlot );
        SW_REQUIRE_EQUAL( simulation.getAliveCount(), static_cast<uint32>( listSlot.size() ) );
        vector<uint32> listUnique = listSlot;
        VectorUtil::sortUnique( listUnique );
        SW_EXPECT_EQUAL( listSlot.size(), listUnique.size() ); // 두 입자가 한 칸을 쥐지 않는다

        for ( const auto& [slot, life] : Util::readLifeBySlot( simulation ) )
        {
            maxSlot   = MathUtil::max( maxSlot, slot );
            auto seen = lastSeen.find( slot );
            if ( seen != lastSeen.end() )
            {
                // 바로 앞 단계에도 있었으면 같은 입자이거나(비율이 한 단계만큼 늘었다) 그 사이 죽고 새 입자가 받은 칸이다(비율이 줄었다). 비었다 돌아왔으면 다시 쓴 칸이다.
                const bool bContinuous   = seen->second._stepIndex + 1u == stepIndex;
                const bool bSameParticle = bContinuous && MathUtil::abs( life - ( seen->second._life + lifeDelta ) ) < 0.001f;
                const bool bReused       = bContinuous == false || life < seen->second._life;
                SW_EXPECT_TRUE( bSameParticle || bReused );
                reusedCount += bReused ? 1u : 0u;
            }
            lastSeen[slot] = SlotState{ life, stepIndex };
        }
    }
    // 살아 있는 수는 넷 안팎이다 — 죽은 칸을 다시 쓰지 않으면 칸이 40 단계 동안 계속 커진다.
    SW_EXPECT_TRUE( maxSlot < 8u );
    SW_EXPECT_TRUE( reusedCount > 0u );
}

/** @brief [VFXMeshSlotsTest] 첫 입자는 0 번 칸부터 받고, 다시 시작하면 칸이 처음으로 돌아간다(같은 씨앗이면 같은 칸) */
SW_TEST_CASE( VFXMeshSlotsTest, SlotsStartAtZeroAndRestartResetsThem )
{
    shared_ptr<const VFXEmitterDef> pDef = make_shared<const VFXEmitterDef>( Util::makeMeshEmitter() );
    VFXCPUSimulation                simulation{ pDef, 3u, 1.0f };
    for ( uint32 stepIndex = 0; stepIndex < 3; ++stepIndex )
        simulation.step( 0.1f, nullptr );
    vector<uint32> listFirst;
    simulation.writeInstanceSlots( listFirst );
    SW_REQUIRE_TRUE( listFirst.empty() == false );
    vector<uint32> listSorted = listFirst;
    VectorUtil::sort( listSorted );
    for ( uint32 index = 0; index < static_cast<uint32>( listSorted.size() ); ++index )
        SW_EXPECT_EQUAL( index, listSorted[index] ); // 아직 죽은 입자가 없다 — 0, 1, 2 … 를 빈틈없이 받는다

    for ( uint32 stepIndex = 0; stepIndex < 20; ++stepIndex )
        simulation.step( 0.1f, nullptr );
    simulation.restart();
    vector<uint32> listEmpty;
    simulation.writeInstanceSlots( listEmpty );
    SW_EXPECT_TRUE( listEmpty.empty() );
    for ( uint32 stepIndex = 0; stepIndex < 3; ++stepIndex )
        simulation.step( 0.1f, nullptr );
    vector<uint32> listAgain;
    simulation.writeInstanceSlots( listAgain );
    SW_EXPECT_TRUE( listAgain == listFirst );
}

/**
 * @brief [VFXMeshSlotsTest] 살아 있는 칸만 보이고, 지난번과 달라진 칸만 토글된다(더티) — 같은 칸 목록을 다시 넘기면 아무 칸도 더티가 되지 않는다
 * @details 죽은 칸 때문에 배치 전체를 더티로 올리면 빌더가 부분 수집을 못 한다(더티가 칸 수의 1/4 을 넘으면 전체 수집이다). 칸 값은 부르는 쪽이 쓴다.
 */
SW_TEST_CASE( VFXMeshSlotsTest, OnlyAliveSlotsAreVisibleAndOnlyChangesAreDirty )
{
    PrimitiveRegistry     registry;
    MeshInstanceBatch     batch{ nullptr, nullptr, nullptr, 8u, MeshInstanceWriter::CPU };
    VFXMeshSlotVisibility visibility;
    visibility.reset( batch );
    SW_EXPECT_TRUE( Util::readVisibleEntries( batch ).empty() ); // 새 배치는 모두 숨긴 채 등록한다
    registry.addInstanceBatch( &batch );
    (void)Util::consumeDirtyEntries( registry, batch );

    visibility.update( batch, vector<uint32>{ 2u, 5u } );
    SW_EXPECT_TRUE( Util::readVisibleEntries( batch ) == ( vector<uint32>{ 2u, 5u } ) );
    SW_EXPECT_TRUE( Util::consumeDirtyEntries( registry, batch ) == ( vector<uint32>{ 2u, 5u } ) );

    visibility.update( batch, vector<uint32>{ 5u, 1u } ); // 2 가 죽고 1 이 태어났다 — 5 는 그대로
    SW_EXPECT_TRUE( Util::readVisibleEntries( batch ) == ( vector<uint32>{ 1u, 5u } ) );
    SW_EXPECT_TRUE( Util::consumeDirtyEntries( registry, batch ) == ( vector<uint32>{ 1u, 2u } ) );
    SW_EXPECT_TRUE( visibility.getVisibleSlots() == ( vector<uint32>{ 5u, 1u } ) );

    visibility.update( batch, vector<uint32>{ 1u, 5u } );
    SW_EXPECT_TRUE( Util::consumeDirtyEntries( registry, batch ).empty() );

    visibility.update( batch, vector<uint32>{} ); // 모두 죽었다
    SW_EXPECT_TRUE( Util::readVisibleEntries( batch ).empty() );
    SW_EXPECT_TRUE( Util::consumeDirtyEntries( registry, batch ) == ( vector<uint32>{ 1u, 5u } ) );
    registry.removeInstanceBatch( &batch );
}
