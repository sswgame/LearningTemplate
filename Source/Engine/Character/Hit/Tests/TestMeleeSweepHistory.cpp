#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace
{
    /** @brief 장부 하나와 프레임 넘기기입니다. 칼날은 X 축 위 (0,0,0) → (1,0,0) 입니다. */
    struct MeleeSweepHistoryTestRig
    {
        static constexpr uint64 kAttackerID = 11;

        GameObjectManager _manager;
        MeleeSweepHistory _ledger;

        void advance( uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                _ledger.runMain( _manager, 1.0f / 60.0f );
            }
        }

        void recordSweep()
        {
            MeleeHitParams params;
            params._radius = 0.2f;
            params._damage = 12.0f;
            const vector<float3> listPrevious;
            vector<float3>       listCurrent;
            listCurrent.push_back( float3{ 0.0f, 0.0f, 0.0f } );
            listCurrent.push_back( float3{ 1.0f, 0.0f, 0.0f } );
            _ledger.record( kAttackerID, params, listPrevious, listCurrent );
        }
    };
} // namespace

/** @brief [MeleeSweepHistoryTest] 그 프레임 안팎의 스윕이 표적을 덮으면 인정하고 기록된 피해를 돌려준다. */
SW_TEST_CASE( MeleeSweepHistoryTest, ConfirmsAClaimTheBladeCovers )
{
    MeleeSweepHistoryTestRig rig;
    rig.advance( 5 );
    rig.recordSweep();
    MeleeHitParams params;
    MeleeHitClaim  claim{ rig._ledger.getFrame() + MeleeSweepHistory::kClaimSlackFrames, 3 };
    SW_EXPECT_TRUE( rig._ledger.verify( MeleeSweepHistoryTestRig::kAttackerID, claim, float3{ 0.5f, 0.6f, 0.0f }, 0.4f, &params ) == MeleeClaimResult::Confirmed );
    SW_EXPECT_NEAR( 12.0f, params._damage, 1e-5f );
}

/** @brief [MeleeSweepHistoryTest] 표적이 칼날 밖이면 스윕이 있어도 거절한다. */
SW_TEST_CASE( MeleeSweepHistoryTest, RefusesATargetOutsideTheBlade )
{
    MeleeSweepHistoryTestRig rig;
    rig.recordSweep();
    const MeleeHitClaim claim{ rig._ledger.getFrame(), 3 };
    SW_EXPECT_TRUE( rig._ledger.verify( MeleeSweepHistoryTestRig::kAttackerID, claim, float3{ 0.5f, 3.0f, 0.0f }, 0.4f, nullptr ) == MeleeClaimResult::OutOfBlade );
}

/** @brief [MeleeSweepHistoryTest] 휘두른 적 없는 주장(다른 쪽의 스윕 · 먼 프레임)은 기록이 없어 거절한다 — 클라이언트가 맞음을 지어낼 수 없다. */
SW_TEST_CASE( MeleeSweepHistoryTest, RefusesAClaimWithoutASweep )
{
    MeleeSweepHistoryTestRig rig;
    rig.recordSweep();
    const float3        center{ 0.5f, 0.1f, 0.0f };
    const MeleeHitClaim sameFrame{ rig._ledger.getFrame(), 3 };
    SW_EXPECT_TRUE( rig._ledger.verify( 99, sameFrame, center, 0.4f, nullptr ) == MeleeClaimResult::NoSweep );
    const MeleeHitClaim farFrame{ rig._ledger.getFrame() + MeleeSweepHistory::kClaimSlackFrames + 1u, 3 };
    SW_EXPECT_TRUE( rig._ledger.verify( MeleeSweepHistoryTestRig::kAttackerID, farFrame, center, 0.4f, nullptr ) == MeleeClaimResult::NoSweep );
}

/** @brief [MeleeSweepHistoryTest] 보관 프레임이 지난 기록은 버린다. */
SW_TEST_CASE( MeleeSweepHistoryTest, DropsRecordsPastTheKeepWindow )
{
    MeleeSweepHistoryTestRig rig;
    rig.recordSweep();
    rig.advance( MeleeSweepHistory::kKeepFrameCount );
    SW_EXPECT_EQUAL( size_t{ 1 }, rig._ledger.getRecordCount() );
    rig.advance( 1 );
    SW_EXPECT_EQUAL( size_t{ 0 }, rig._ledger.getRecordCount() );
}

/** @brief [MeleeSweepHistoryTest] 인정되면 표적을 창의 맞힘 목록에 더하고, 이미 있으면 더하지 않는다(창 하나에 한 번). 거절이면 목록은 그대로다. */
SW_TEST_CASE( MeleeSweepHistoryTest, ClaimHitAddsTheTargetOncePerWindow )
{
    MeleeSweepHistoryTestRig rig;
    GameObject*              pAttacker = rig._manager.createGameObject( hashed_string( "Attacker" ) );
    GameObject*              pTarget   = rig._manager.createGameObject( hashed_string( "Target" ) );
    MeleeHitParams           params;
    params._radius = 0.2f;
    params._damage = 5.0f;
    const vector<float3> listPrevious;
    vector<float3>       listCurrent;
    listCurrent.push_back( float3{ 0.0f, 0.0f, 0.0f } );
    listCurrent.push_back( float3{ 1.0f, 0.0f, 0.0f } );
    rig._ledger.record( pAttacker->getObjectID(), params, listPrevious, listCurrent );

    vector<GameObjectHandle> listHitTarget;
    const MeleeHitClaim      claim{ rig._ledger.getFrame(), 3 };
    const float3             center{ 0.5f, 0.1f, 0.0f };
    SW_EXPECT_TRUE( rig._ledger.claimHit( *pAttacker, *pTarget, claim, center, 0.4f, listHitTarget ) == MeleeClaimResult::Confirmed );
    SW_EXPECT_EQUAL( size_t{ 1 }, listHitTarget.size() );
    SW_EXPECT_TRUE( rig._ledger.claimHit( *pAttacker, *pTarget, claim, center, 0.4f, listHitTarget ) == MeleeClaimResult::Confirmed );
    SW_EXPECT_EQUAL( size_t{ 1 }, listHitTarget.size() );

    vector<GameObjectHandle> listOther;
    SW_EXPECT_TRUE( rig._ledger.claimHit( *pAttacker, *pTarget, claim, float3{ 0.5f, 9.0f, 0.0f }, 0.4f, listOther ) == MeleeClaimResult::OutOfBlade );
    SW_EXPECT_TRUE( listOther.empty() );
}

/** @brief [MeleeSweepHistoryTest] 주장의 전송 형식이 값을 그대로 옮기고, 모자란 바이트는 거절한다. */
SW_TEST_CASE( MeleeSweepHistoryTest, ClaimWireFormatRoundTrips )
{
    BitWriter           writer;
    const MeleeHitClaim sent{ 77, 123456789012ull };
    sent.write( writer );
    BitReader     reader( writer.getBytes().data(), writer.getByteCount() );
    MeleeHitClaim received;
    SW_REQUIRE_TRUE( MeleeHitClaim::read( reader, received ) );
    SW_EXPECT_EQUAL( 77u, received._frame );
    SW_EXPECT_EQUAL( uint64{ 123456789012ull }, received._targetID );

    BitReader empty( writer.getBytes().data(), 0 );
    SW_EXPECT_FALSE( MeleeHitClaim::read( empty, received ) );
}
