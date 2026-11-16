#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Games/LanternVale/ValeWorld.h"

#include <limits>

// 골짜기 규칙 — 서버가 판정하는 전투(사거리 · 재사용 대기 · 쓰러뜨림 · 다시 나기) · 채집 · 이동 상한 · 성장(단계표) · 파티(초대 · 수락 · 경험 나눔 · 떠남), 진행 바이트.

using namespace sw;

namespace
{
    constexpr uint32 kNearCreatureID = ValeWorld::kPlacementIDBase + 0;
    constexpr uint32 kSideCreatureID = ValeWorld::kPlacementIDBase + 1;
    constexpr uint32 kNearHerbID     = ValeWorld::kPlacementIDBase + 2;
    constexpr uint32 kFarCreatureID  = ValeWorld::kPlacementIDBase + 3;

    /** @brief 씬(`vale.scene.xml`)과 같은 수치의 규칙입니다. */
    ValeRules makeValeTestRules()
    {
        ValeRules rules;
        rules._listLevelExperience = { 100, 250, 450, 700 };
        rules._moveSpeed           = 6.0f; // 씬과 같다
        ValePlacement spawn;
        spawn._kind     = ValePlacementKind::Creature;
        spawn._position = float3{ 2.0f, 0.0f, 0.0f };
        rules._listPlacement.push_back( spawn );
        spawn._position = float3{ -2.0f, 0.0f, 1.0f };
        rules._listPlacement.push_back( spawn );
        spawn._kind     = ValePlacementKind::Herb;
        spawn._position = float3{ 0.0f, 0.0f, 1.5f };
        rules._listPlacement.push_back( spawn );
        spawn._kind     = ValePlacementKind::Creature;
        spawn._position = float3{ 20.0f, 0.0f, 18.0f };
        rules._listPlacement.push_back( spawn );
        return rules;
    }

    /** @brief 짐승 하나를 쓰러질 때까지 칩니다(사이마다 재사용 대기를 흘린다). */
    void attackUntilDefeated( ValeWorld& world, uint32 avatarID, uint32 creatureID )
    {
        for ( int32 strike = 0; strike < 3; ++strike )
        {
            SW_EXPECT_TRUE( world.attackCreature( avatarID, creatureID ) == ValeActionResult::Ok );
            world.tick( 0.6f );
        }
    }
} // namespace

/** @brief [ValeWorldTest] 짐승은 세 번에 쓰러지고 경험 · 동전 · 처치가 오르며, 다시 날 때까지 대상이 없다. */
SW_TEST_CASE( ValeWorldTest, StrikesFellACreatureAndRewardTheStriker )
{
    ValeWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeValeTestRules() ) );
    const uint32 avatarID = world.addAvatar( 1, ValeProfile{} );
    attackUntilDefeated( world, avatarID, kNearCreatureID );
    const ValeAvatar* pAvatar = world.findAvatar( avatarID );
    SW_REQUIRE_NOT_NULL( pAvatar );
    SW_EXPECT_EQUAL( 60, pAvatar->_profile._experience );
    SW_EXPECT_EQUAL( 5, pAvatar->_profile._coins );
    SW_EXPECT_EQUAL( 1, pAvatar->_profile._defeatCount );
    SW_EXPECT_EQUAL( 1, pAvatar->_profile._level );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearCreatureID ) == ValeActionResult::TargetGone );
    world.tick( 3.1f );
    SW_EXPECT_EQUAL( 30, world.findPlacement( kNearCreatureID )->_health );
    SW_EXPECT_TRUE( world.findPlacement( kNearCreatureID )->_bPresent != SW_FALSE );
}

/** @brief [ValeWorldTest] 재사용 대기 · 사거리 · 없는 대상 · 갈래가 다른 번호는 서버가 거절한다. */
SW_TEST_CASE( ValeWorldTest, ServerRefusesCooldownRangeAndWrongTargets )
{
    ValeWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeValeTestRules() ) );
    const uint32 avatarID = world.addAvatar( 1, ValeProfile{} );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearCreatureID ) == ValeActionResult::Ok );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearCreatureID ) == ValeActionResult::CoolingDown );
    world.tick( 0.6f );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kFarCreatureID ) == ValeActionResult::OutOfRange );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearHerbID ) == ValeActionResult::UnknownTarget );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearCreatureID ) == ValeActionResult::UnknownTarget );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, 7 ) == ValeActionResult::UnknownTarget );
    SW_EXPECT_TRUE( world.isPlacementOfKind( kNearCreatureID, ValePlacementKind::Creature ) );
    SW_EXPECT_FALSE( world.isPlacementOfKind( kNearHerbID, ValePlacementKind::Creature ) );
    SW_EXPECT_FALSE( world.isPlacementOfKind( ValeWorld::kPlacementIDBase + 99, ValePlacementKind::Creature ) );
}

/** @brief [ValeWorldTest] 채집은 거리 안에서만, 이동은 한 걸음 상한과 경계 안에서만 된다. */
SW_TEST_CASE( ValeWorldTest, GatherAndMoveFollowTheServerLimits )
{
    ValeWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeValeTestRules() ) );
    const uint32 avatarID = world.addAvatar( 1, ValeProfile{} );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == ValeActionResult::Ok );
    SW_EXPECT_EQUAL( 1, world.findAvatar( avatarID )->_profile._herbCount );
    SW_EXPECT_EQUAL( 15, world.findAvatar( avatarID )->_profile._experience );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == ValeActionResult::TargetGone );

    SW_EXPECT_TRUE( world.moveAvatar( avatarID, 30.0f, 0.0f ) == ValeActionResult::StepTooLong );
    SW_EXPECT_TRUE( world.moveAvatar( avatarID, 8.0f, 6.0f ) == ValeActionResult::Ok );
    SW_EXPECT_NEAR( 8.0, world.findAvatar( avatarID )->_profile._position._x, 1e-4 );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == ValeActionResult::TargetGone );
    world.tick( 5.1f );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == ValeActionResult::OutOfRange );
    SW_EXPECT_FALSE( world.isInsideVale( 70.0f, 0.0f ) );
    SW_EXPECT_FALSE( world.isInsideVale( std::numeric_limits<float32>::quiet_NaN(), 0.0f ) );
}

/** @brief [ValeWorldTest] 경험이 단계표를 넘은 만큼 레벨이 오른다(한 번에 둘도). */
SW_TEST_CASE( ValeWorldTest, ExperienceClimbsTheLevelTable )
{
    ValeWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeValeTestRules() ) );
    ValeProfile profile;
    profile._experience   = 240;
    profile._level        = 2;
    const uint32 avatarID = world.addAvatar( 1, profile );
    attackUntilDefeated( world, avatarID, kNearCreatureID );
    SW_EXPECT_EQUAL( 300, world.findAvatar( avatarID )->_profile._experience );
    SW_EXPECT_EQUAL( 3, world.findAvatar( avatarID )->_profile._level );

    ValeProfile veteran;
    veteran._experience  = 440;
    const uint32 otherID = world.addAvatar( 2, veteran );
    attackUntilDefeated( world, otherID, kSideCreatureID );
    SW_EXPECT_EQUAL( 4, world.findAvatar( otherID )->_profile._level ); // 1 에서 시작해 100 · 250 · 450 을 한 번에 넘는다
}

/** @brief [ValeWorldTest] 파티는 초대 → 수락으로 서고, 나눔 거리 안의 파티원이 경험을 똑같이(올림) 나누며, 동전은 친 사람만 갖는다. */
SW_TEST_CASE( ValeWorldTest, PartySharesExperience )
{
    ValeWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeValeTestRules() ) );
    const uint32 leaderID = world.addAvatar( 1, ValeProfile{} );
    const uint32 memberID = world.addAvatar( 2, ValeProfile{} );
    SW_EXPECT_TRUE( world.acceptPartyInvite( memberID, leaderID ) == ValeActionResult::NotInvited );
    SW_EXPECT_TRUE( world.inviteToParty( leaderID, memberID ) == ValeActionResult::Ok );
    SW_EXPECT_TRUE( world.acceptPartyInvite( memberID, leaderID ) == ValeActionResult::Ok );
    SW_EXPECT_EQUAL( 2, world.getPartySize( leaderID ) );
    SW_EXPECT_EQUAL( 2, world.getPartySize( memberID ) );

    attackUntilDefeated( world, leaderID, kNearCreatureID );
    SW_EXPECT_EQUAL( 30, world.findAvatar( leaderID )->_profile._experience );
    SW_EXPECT_EQUAL( 30, world.findAvatar( memberID )->_profile._experience );
    SW_EXPECT_EQUAL( 5, world.findAvatar( leaderID )->_profile._coins );
    SW_EXPECT_EQUAL( 0, world.findAvatar( memberID )->_profile._coins );

    // 나눔 거리 밖으로 간 파티원은 받지 않는다(한 걸음 12 m 씩 걸어 40 m 밖으로). 걸음 사이에는 시간이 흘러야 한다 — 틱 없이 몰아 보낸 걸음은 거절된다(감사 K-1).
    SW_EXPECT_TRUE( world.moveAvatar( memberID, 0.0f, 12.0f ) == ValeActionResult::Ok );
    SW_EXPECT_TRUE( world.moveAvatar( memberID, 0.0f, 24.0f ) == ValeActionResult::StepTooLong );
    world.tick( 12.0f / 6.0f ); // 걷는 속도 6 m/s 로 12 m
    SW_EXPECT_TRUE( world.moveAvatar( memberID, 0.0f, 24.0f ) == ValeActionResult::Ok );
    world.tick( 12.0f / 6.0f );
    SW_EXPECT_TRUE( world.moveAvatar( memberID, 0.0f, 36.0f ) == ValeActionResult::Ok );
    attackUntilDefeated( world, leaderID, kSideCreatureID );
    SW_EXPECT_EQUAL( 90, world.findAvatar( leaderID )->_profile._experience );
    SW_EXPECT_EQUAL( 30, world.findAvatar( memberID )->_profile._experience );

    // 둘 가운데 하나가 떠나면 파티가 풀린다.
    SW_EXPECT_TRUE( world.leaveParty( memberID ) == ValeActionResult::Ok );
    SW_EXPECT_EQUAL( 1, world.getPartySize( leaderID ) );
    SW_EXPECT_TRUE( world.leaveParty( leaderID ) == ValeActionResult::NotInParty );
}

/** @brief [ValeWorldTest] 이미 파티에 든 사람 · 가득 찬 파티 · 떠난 사람의 초대는 받지 않는다. */
SW_TEST_CASE( ValeWorldTest, PartyRefusesFullTakenAndStaleInvites )
{
    ValeRules rules     = makeValeTestRules();
    rules._partyMaxSize = 2;
    ValeWorld world;
    SW_REQUIRE_TRUE( world.initialize( rules ) );
    const uint32 firstID  = world.addAvatar( 1, ValeProfile{} );
    const uint32 secondID = world.addAvatar( 2, ValeProfile{} );
    const uint32 thirdID  = world.addAvatar( 3, ValeProfile{} );
    SW_EXPECT_TRUE( world.inviteToParty( firstID, secondID ) == ValeActionResult::Ok );
    SW_EXPECT_TRUE( world.inviteToParty( firstID, thirdID ) == ValeActionResult::Ok );
    SW_EXPECT_TRUE( world.acceptPartyInvite( secondID, firstID ) == ValeActionResult::Ok );
    SW_EXPECT_TRUE( world.acceptPartyInvite( thirdID, firstID ) == ValeActionResult::PartyFull );
    SW_EXPECT_TRUE( world.inviteToParty( thirdID, secondID ) == ValeActionResult::AlreadyInParty );
    SW_EXPECT_TRUE( world.inviteToParty( firstID, firstID ) == ValeActionResult::UnknownTarget );

    world.removeAvatar( firstID ); // 이끈 사람이 나가면 둘 파티가 풀린다
    SW_EXPECT_EQUAL( 1, world.getPartySize( secondID ) );
    SW_EXPECT_TRUE( world.acceptPartyInvite( thirdID, firstID ) == ValeActionResult::UnknownTarget );
}

/** @brief [ValeWorldTest] 단계표가 오름차순이 아니거나 놓을 것이 경계 밖이면 골짜기를 세우지 않는다. */
SW_TEST_CASE( ValeWorldTest, InvalidRulesAreRefused )
{
    ValeRules descending            = makeValeTestRules();
    descending._listLevelExperience = { 100, 90 };
    ValeWorld world;
    SW_EXPECT_LOG( "LanternVale.LevelTableNotAscending" );
    SW_EXPECT_FALSE( world.initialize( descending ) );

    ValeRules outside                          = makeValeTestRules();
    outside._listPlacement.back()._position._x = 500.0f;
    SW_EXPECT_LOG( "LanternVale.PlacementOutside" );
    SW_EXPECT_FALSE( world.initialize( outside ) );

    ValeRules empty = makeValeTestRules();
    empty._listPlacement.clear();
    SW_EXPECT_LOG( "LanternVale.PlacementListInvalid" );
    SW_EXPECT_FALSE( world.initialize( empty ) );
}

/** @brief [ValeWorldTest] 진행 바이트는 같은 값으로 돌아오고, 판이 다르거나 값이 범위 밖이면 읽지 않는다. */
SW_TEST_CASE( ValeWorldTest, ProfileBytesRoundTrip )
{
    ValeProfile saved;
    saved._level       = 3;
    saved._experience  = 321;
    saved._coins       = 17;
    saved._herbCount   = 4;
    saved._defeatCount = 9;
    saved._position    = float3{ 8.0f, 0.0f, 6.0f };
    BitWriter writer;
    saved.write( writer );
    const vector<uint8> bytes = writer.releaseBytes();
    ValeProfile         loaded;
    BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_REQUIRE_TRUE( loaded.read( reader ) );
    SW_EXPECT_EQUAL( 3, loaded._level );
    SW_EXPECT_EQUAL( 321, loaded._experience );
    SW_EXPECT_EQUAL( 17, loaded._coins );
    SW_EXPECT_EQUAL( 4, loaded._herbCount );
    SW_EXPECT_EQUAL( 9, loaded._defeatCount );
    SW_EXPECT_NEAR( 6.0, loaded._position._z, 1e-6 );

    ValeProfile negative;
    negative._coins = -1;
    BitWriter badWriter;
    negative.write( badWriter );
    const vector<uint8> badBytes = badWriter.releaseBytes();
    BitReader           badReader( badBytes.data(), static_cast<int32>( badBytes.size() ) );
    ValeProfile         untouched;
    SW_EXPECT_FALSE( untouched.read( badReader ) );
    SW_EXPECT_EQUAL( 0, untouched._coins );

    BitWriter otherVersion;
    otherVersion.writeVarUint( ValeProfile::kFormatVersion + 1 );
    const vector<uint8> otherBytes = otherVersion.releaseBytes();
    BitReader           otherReader( otherBytes.data(), static_cast<int32>( otherBytes.size() ) );
    SW_EXPECT_FALSE( untouched.read( otherReader ) );
}
