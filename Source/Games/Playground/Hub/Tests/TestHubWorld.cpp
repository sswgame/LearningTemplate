#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Games/Playground/Hub/HubWorld.h"

#include <limits>
#include <type_traits>

// 허브 규칙 — 서버가 판정하는 전투(사거리 · 재사용 대기 · 쓰러뜨림 · 다시 나기) · 채집 · 이동 상한 · 성장(단계표) · 파티(초대 · 수락 · 경험 나눔 · 떠남), 진행 바이트.

using namespace sw;

// 결함 41653ecd(이동 예산을 서버 프레임의 시간이 채워, 서버가 프레임을 놓치면 한도 속도로 걷는 정직한 걸음이 되돌려졌다): 예산은 그 걸음의 시간만 받는다 —
// 프레임 시간(게임 시간) · 실제 시간을 넣는 꼴이 컴파일되지 않는다.
static_assert( std::is_invocable_v<decltype( &HubWorld::moveAvatar ), HubWorld&, uint32, const AbsolutePosition&, StepSeconds, bool>, "a move spends its own step time" );
static_assert( std::is_invocable_v<decltype( &HubWorld::moveAvatar ), HubWorld&, uint32, const AbsolutePosition&, GameSeconds, bool> == false, "a server frame time is not the step time" );
static_assert( std::is_invocable_v<decltype( &HubWorld::moveAvatar ), HubWorld&, uint32, const AbsolutePosition&, RealSeconds, bool> == false, "a wall clock time is not the step time" );

namespace
{
    constexpr uint32 kNearCreatureID = HubWorld::kPlacementIDBase + 0;
    constexpr uint32 kSideCreatureID = HubWorld::kPlacementIDBase + 1;
    constexpr uint32 kNearHerbID     = HubWorld::kPlacementIDBase + 2;
    constexpr uint32 kFarCreatureID  = HubWorld::kPlacementIDBase + 3;

    /** @brief 씬(`hub.scene.xml`)과 같은 수치의 규칙입니다. */
    HubRules makeHubTestRules()
    {
        HubRules rules;
        rules._listLevelExperience = { 100, 250, 450, 700 };
        rules._moveSpeed           = 6.0f;  // 씬과 같다
        rules._rideSpeed           = 12.0f; // 씬과 같다
        rules._listAvatarStart     = {
            RespawnSpawnPoint{hashed_string( "hub_0" ),  float3{ 0.0f, 0.0f, 0.0f }, 0.0f, -1},
            RespawnSpawnPoint{hashed_string( "hub_1" ), float3{ 1.5f, 0.0f, -1.5f }, 0.0f, -1}
        };
        HubPlacement spawn;
        spawn._kind     = HubPlacementKind::Creature;
        spawn._position = float3{ 2.0f, 0.0f, 0.0f };
        rules._listPlacement.push_back( spawn );
        spawn._position = float3{ -2.0f, 0.0f, 1.0f };
        rules._listPlacement.push_back( spawn );
        spawn._kind     = HubPlacementKind::Herb;
        spawn._position = float3{ 0.0f, 0.0f, 1.5f };
        rules._listPlacement.push_back( spawn );
        spawn._kind     = HubPlacementKind::Creature;
        spawn._position = float3{ 20.0f, 0.0f, 18.0f };
        rules._listPlacement.push_back( spawn );
        return rules;
    }

    /** @brief 짐승 하나를 쓰러질 때까지 칩니다(사이마다 재사용 대기를 흘린다). */
    void attackUntilDefeated( HubWorld& world, uint32 avatarID, uint32 creatureID )
    {
        for ( int32 strike = 0; strike < 3; ++strike )
        {
            SW_EXPECT_TRUE( world.attackCreature( avatarID, creatureID ) == HubActionResult::Ok );
            world.update( 0.6f );
        }
    }
} // namespace

/** @brief [HubWorldTest] 짐승은 세 번에 쓰러지고 경험 · 동전 · 처치가 오르며, 다시 날 때까지 대상이 없다. */
SW_TEST_CASE( HubWorldTest, StrikesFellACreatureAndRewardTheStriker )
{
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeHubTestRules() ) );
    const uint32 avatarID = world.addAvatar( 1, HubProfile{} );
    attackUntilDefeated( world, avatarID, kNearCreatureID );
    const HubAvatar* pAvatar = world.findAvatar( avatarID );
    SW_REQUIRE_NOT_NULL( pAvatar );
    SW_EXPECT_EQUAL( 60, pAvatar->_profile._experience );
    SW_EXPECT_EQUAL( 5, pAvatar->_profile._coins );
    SW_EXPECT_EQUAL( 1, pAvatar->_profile._defeatCount );
    SW_EXPECT_EQUAL( 1, pAvatar->_profile._level );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearCreatureID ) == HubActionResult::TargetGone );
    world.update( 3.1f );
    SW_EXPECT_EQUAL( 30, world.findPlacement( kNearCreatureID )->_health );
    SW_EXPECT_TRUE( world.findPlacement( kNearCreatureID )->_bPresent != SW_FALSE );
}

/** @brief [HubWorldTest] 재사용 대기 · 사거리 · 없는 대상 · 갈래가 다른 번호는 서버가 거절한다. */
SW_TEST_CASE( HubWorldTest, ServerRefusesCooldownRangeAndWrongTargets )
{
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeHubTestRules() ) );
    const uint32 avatarID = world.addAvatar( 1, HubProfile{} );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearCreatureID ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearCreatureID ) == HubActionResult::CoolingDown );
    world.update( 0.6f );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kFarCreatureID ) == HubActionResult::OutOfRange );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, kNearHerbID ) == HubActionResult::UnknownTarget );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearCreatureID ) == HubActionResult::UnknownTarget );
    SW_EXPECT_TRUE( world.attackCreature( avatarID, 7 ) == HubActionResult::UnknownTarget );
    SW_EXPECT_TRUE( world.isPlacementOfKind( kNearCreatureID, HubPlacementKind::Creature ) );
    SW_EXPECT_FALSE( world.isPlacementOfKind( kNearHerbID, HubPlacementKind::Creature ) );
    SW_EXPECT_FALSE( world.isPlacementOfKind( HubWorld::kPlacementIDBase + 99, HubPlacementKind::Creature ) );
}

/** @brief [HubWorldTest] 채집은 거리 안에서만, 이동은 한 걸음 상한과 경계 안에서만 된다. */
SW_TEST_CASE( HubWorldTest, GatherAndMoveFollowTheServerLimits )
{
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeHubTestRules() ) );
    const uint32 avatarID = world.addAvatar( 1, HubProfile{} );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == HubActionResult::Ok );
    SW_EXPECT_EQUAL( 1, world.findAvatar( avatarID )->_profile._herbCount );
    SW_EXPECT_EQUAL( 15, world.findAvatar( avatarID )->_profile._experience );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == HubActionResult::TargetGone );

    SW_EXPECT_TRUE( world.moveAvatar( avatarID, AbsolutePosition{
                                                    float3{ 30.0f, 0.0f, 0.0f }
    },
                                      StepSeconds{ 0.0f }, false ) == HubActionResult::StepTooLong );
    SW_EXPECT_TRUE( world.moveAvatar( avatarID, AbsolutePosition{
                                                    float3{ 8.0f, 0.0f, 6.0f }
    },
                                      StepSeconds{ 0.0f }, false ) == HubActionResult::Ok );
    SW_EXPECT_NEAR( 8.0, world.findAvatar( avatarID )->_profile._position._x, 1e-4 );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == HubActionResult::TargetGone );
    world.update( 5.1f );
    SW_EXPECT_TRUE( world.gatherHerb( avatarID, kNearHerbID ) == HubActionResult::OutOfRange );
    SW_EXPECT_FALSE( world.isInsideHub( 70.0f, 0.0f ) );
    SW_EXPECT_FALSE( world.isInsideHub( std::numeric_limits<float32>::quiet_NaN(), 0.0f ) );
}

/** @brief [HubWorldTest] 탄 동안의 속도 한도는 따로다 — 말의 질주(10 m/s)는 받고, 걷는 몸이 같은 거리를 가면 거절한다. 거절하면 자리는 그대로다. */
SW_TEST_CASE( HubWorldTest, RidingHasItsOwnSpeedLimit )
{
    HubRules rules     = makeHubTestRules();
    rules._maxMoveStep = 2.0f; // 모아 둔 몫이 짧게 떨어지게
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( rules ) );
    const uint32 riderID  = world.addAvatar( 1, HubProfile{} );
    const uint32 walkerID = world.addAvatar( 2, HubProfile{} );
    SW_EXPECT_TRUE( world.moveAvatar( riderID, AbsolutePosition{
                                                   float3{ 0.0f, 0.0f, 2.0f }
    },
                                      StepSeconds{ 0.0f }, true ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.moveAvatar( walkerID, AbsolutePosition{
                                                    float3{ 0.0f, 0.0f, 2.0f }
    },
                                      StepSeconds{ 0.0f }, false ) == HubActionResult::Ok );
    constexpr float32 kStep   = 0.1f;
    int32             refused = 0;
    for ( int32 step = 1; step <= 20; ++step )
    {
        const float32 z = 2.0f + 10.0f * kStep * static_cast<float32>( step );
        SW_EXPECT_TRUE( world.moveAvatar( riderID, AbsolutePosition{
                                                       float3{ 0.0f, 0.0f, z }
        },
                                          StepSeconds{ kStep }, true ) == HubActionResult::Ok );
        refused += world.moveAvatar( walkerID, AbsolutePosition{
                                                   float3{ 0.0f, 0.0f, z }
        },
                                     StepSeconds{ kStep }, false ) == HubActionResult::Ok
                     ? 0
                     : 1;
    }
    SW_EXPECT_NEAR( 22.0f, world.findAvatar( riderID )->_profile._position._z, 1.0e-3f );
    SW_EXPECT_TRUE( refused > 0 );
    SW_EXPECT_TRUE( world.findAvatar( walkerID )->_profile._position._z < 22.0f );
    SW_EXPECT_TRUE( world.moveAvatar( riderID, AbsolutePosition{
                                                   float3{ 0.0f, std::numeric_limits<float32>::quiet_NaN(), 22.0f }
    },
                                      StepSeconds{ 0.0f }, true ) == HubActionResult::StepTooLong );
}

/**
 * @brief [HubWorldTest] 결함: 이동 예산을 게임 시간이 채워, 실제 시간으로 걷는 클라이언트가 서버의 놓친 프레임만큼 예산을 잃고 정직한 걸음을 되돌려받았다 — 예산은 걸음의 시간이 채운다
 * @details 서버의 게임 시간이 걸음 시간의 반만 흐르는(밀려 프레임을 버리는) 동안 한도 속도로 걷는다. 한도를 넘는 걸음은 모아 둔 몫을 쓴 뒤 거절된다.
 */
SW_TEST_CASE( HubWorldTest, MoveBudgetFollowsTheStepTimeNotTheServerFrames )
{
    HubRules rules     = makeHubTestRules();
    rules._maxMoveStep = 2.0f;
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( rules ) );
    const uint32      avatarID = world.addAvatar( 1, HubProfile{} );
    constexpr float32 kStep    = 0.1f;
    float32           z        = 0.0f;
    for ( int32 step = 0; step < 60; ++step )
    {
        world.update( kStep * 0.5f );
        z += 6.0f * kStep;
        SW_REQUIRE_TRUE( world.moveAvatar( avatarID, AbsolutePosition{
                                                         float3{ 0.0f, 0.0f, z }
        },
                                           StepSeconds{ kStep }, false ) == HubActionResult::Ok );
    }
    int32 refused = 0;
    for ( int32 step = 0; step < 20; ++step )
    {
        const float32 next = z + 9.0f * kStep;
        if ( world.moveAvatar( avatarID, AbsolutePosition{
                                             float3{ 0.0f, 0.0f, next }
        },
                               StepSeconds{ kStep }, false ) == HubActionResult::Ok )
            z = next;
        else
            ++refused;
    }
    SW_EXPECT_TRUE( refused > 0 );
}

/** @brief [HubWorldTest] 새 아바타는 앞선 시작 자리 가운데 빈 곳에 서고, 다 찼으면 아바타에서 가장 먼 곳에 선다. 저장된 자리가 밖이면 시작 자리로 온다. */
SW_TEST_CASE( HubWorldTest, NewAvatarsTakeFreeStartPoints )
{
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeHubTestRules() ) );
    HubProfile profile;
    profile._position    = world.chooseAvatarStart();
    const uint32 firstID = world.addAvatar( 1, profile );
    SW_EXPECT_TRUE( world.findAvatar( firstID )->_profile._position == ( float3{ 0.0f, 0.0f, 0.0f } ) );
    profile._position     = world.chooseAvatarStart();
    const uint32 secondID = world.addAvatar( 2, profile );
    SW_EXPECT_TRUE( world.findAvatar( secondID )->_profile._position == ( float3{ 1.5f, 0.0f, -1.5f } ) );

    // 둘 다 찼다 — 첫 아바타가 0.5 m 비켜 선 첫 자리가 더 멀다.
    SW_EXPECT_TRUE( world.moveAvatar( firstID, AbsolutePosition{
                                                   float3{ 0.0f, 0.0f, 0.5f }
    },
                                      StepSeconds{ 0.0f }, false ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.chooseAvatarStart() == ( float3{ 0.0f, 0.0f, 0.0f } ) );

    world.removeAvatar( secondID );
    profile._position    = float3{ 500.0f, 0.0f, 0.0f }; // 규칙이 바뀌어 저장된 자리가 밖이다
    const uint32 thirdID = world.addAvatar( 3, profile );
    SW_EXPECT_TRUE( world.findAvatar( thirdID )->_profile._position == ( float3{ 1.5f, 0.0f, -1.5f } ) );
}

/** @brief [HubWorldTest] 시작 자리가 없거나 경계 밖이면 허브를 세우지 않는다. */
SW_TEST_CASE( HubWorldTest, StartPointsAreRequiredInsideTheHub )
{
    HubWorld world;
    HubRules missing = makeHubTestRules();
    missing._listAvatarStart.clear();
    SW_EXPECT_LOG( "Hub.AvatarStartMissing" );
    SW_EXPECT_FALSE( world.initialize( missing ) );
    HubRules outside                             = makeHubTestRules();
    outside._listAvatarStart.back()._position._x = 500.0f;
    SW_EXPECT_LOG( "Hub.AvatarStartOutside" );
    SW_EXPECT_FALSE( world.initialize( outside ) );
}

/** @brief [HubWorldTest] 폰의 걸음은 허브 경계 안으로 잘린다 — 서버와 예측이 같은 함수다. */
SW_TEST_CASE( HubWorldTest, PositionsAreClampedInsideTheHub )
{
    const HubRules rules   = makeHubTestRules();
    const float3   clamped = rules.clampInside( AbsolutePosition{
                                                  float3{ 100.0f, 3.0f, -100.0f }
    } )
                               ._value;
    SW_EXPECT_TRUE( clamped == ( float3{ rules._worldHalfExtent, 3.0f, -rules._worldHalfExtent } ) );
}

/** @brief [HubWorldTest] 경험이 단계표를 넘은 만큼 레벨이 오른다(한 번에 둘도). */
SW_TEST_CASE( HubWorldTest, ExperienceClimbsTheLevelTable )
{
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeHubTestRules() ) );
    HubProfile profile;
    profile._experience   = 240;
    profile._level        = 2;
    const uint32 avatarID = world.addAvatar( 1, profile );
    attackUntilDefeated( world, avatarID, kNearCreatureID );
    SW_EXPECT_EQUAL( 300, world.findAvatar( avatarID )->_profile._experience );
    SW_EXPECT_EQUAL( 3, world.findAvatar( avatarID )->_profile._level );

    HubProfile veteran;
    veteran._experience  = 440;
    const uint32 otherID = world.addAvatar( 2, veteran );
    attackUntilDefeated( world, otherID, kSideCreatureID );
    SW_EXPECT_EQUAL( 4, world.findAvatar( otherID )->_profile._level ); // 1 에서 시작해 100 · 250 · 450 을 한 번에 넘는다
}

/** @brief [HubWorldTest] 파티는 초대 → 수락으로 서고, 나눔 거리 안의 파티원이 경험을 똑같이(올림) 나누며, 동전은 친 사람만 갖는다. */
SW_TEST_CASE( HubWorldTest, PartySharesExperience )
{
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( makeHubTestRules() ) );
    const uint32 leaderID = world.addAvatar( 1, HubProfile{} );
    const uint32 memberID = world.addAvatar( 2, HubProfile{} );
    SW_EXPECT_TRUE( world.acceptPartyInvite( memberID, leaderID ) == HubActionResult::NotInvited );
    SW_EXPECT_TRUE( world.inviteToParty( leaderID, memberID ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.acceptPartyInvite( memberID, leaderID ) == HubActionResult::Ok );
    SW_EXPECT_EQUAL( 2, world.getPartySize( leaderID ) );
    SW_EXPECT_EQUAL( 2, world.getPartySize( memberID ) );

    attackUntilDefeated( world, leaderID, kNearCreatureID );
    SW_EXPECT_EQUAL( 30, world.findAvatar( leaderID )->_profile._experience );
    SW_EXPECT_EQUAL( 30, world.findAvatar( memberID )->_profile._experience );
    SW_EXPECT_EQUAL( 5, world.findAvatar( leaderID )->_profile._coins );
    SW_EXPECT_EQUAL( 0, world.findAvatar( memberID )->_profile._coins );

    // 나눔 거리 밖으로 간 파티원은 받지 않는다(한 걸음 12 m 씩 걸어 40 m 밖으로). 걸음은 제 시간만큼만 간다 — 시간 없이 몰아 보낸 걸음은 거절된다(감사 K-1).
    SW_EXPECT_TRUE( world.moveAvatar( memberID, AbsolutePosition{
                                                    float3{ 0.0f, 0.0f, 12.0f }
    },
                                      StepSeconds{ 0.0f }, false ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.moveAvatar( memberID, AbsolutePosition{
                                                    float3{ 0.0f, 0.0f, 24.0f }
    },
                                      StepSeconds{ 0.0f }, false ) == HubActionResult::StepTooLong );
    constexpr float32 kStepSeconds = 12.0f / 6.0f; // 걷는 속도 6 m/s 로 12 m
    SW_EXPECT_TRUE( world.moveAvatar( memberID, AbsolutePosition{
                                                    float3{ 0.0f, 0.0f, 24.0f }
    },
                                      StepSeconds{ kStepSeconds }, false ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.moveAvatar( memberID, AbsolutePosition{
                                                    float3{ 0.0f, 0.0f, 36.0f }
    },
                                      StepSeconds{ kStepSeconds }, false ) == HubActionResult::Ok );
    attackUntilDefeated( world, leaderID, kSideCreatureID );
    SW_EXPECT_EQUAL( 90, world.findAvatar( leaderID )->_profile._experience );
    SW_EXPECT_EQUAL( 30, world.findAvatar( memberID )->_profile._experience );

    // 둘 가운데 하나가 떠나면 파티가 풀린다.
    SW_EXPECT_TRUE( world.leaveParty( memberID ) == HubActionResult::Ok );
    SW_EXPECT_EQUAL( 1, world.getPartySize( leaderID ) );
    SW_EXPECT_TRUE( world.leaveParty( leaderID ) == HubActionResult::NotInParty );
}

/** @brief [HubWorldTest] 이미 파티에 든 사람 · 가득 찬 파티 · 떠난 사람의 초대는 받지 않는다. */
SW_TEST_CASE( HubWorldTest, PartyRefusesFullTakenAndStaleInvites )
{
    HubRules rules      = makeHubTestRules();
    rules._partyMaxSize = 2;
    HubWorld world;
    SW_REQUIRE_TRUE( world.initialize( rules ) );
    const uint32 firstID  = world.addAvatar( 1, HubProfile{} );
    const uint32 secondID = world.addAvatar( 2, HubProfile{} );
    const uint32 thirdID  = world.addAvatar( 3, HubProfile{} );
    SW_EXPECT_TRUE( world.inviteToParty( firstID, secondID ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.inviteToParty( firstID, thirdID ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.acceptPartyInvite( secondID, firstID ) == HubActionResult::Ok );
    SW_EXPECT_TRUE( world.acceptPartyInvite( thirdID, firstID ) == HubActionResult::PartyFull );
    SW_EXPECT_TRUE( world.inviteToParty( thirdID, secondID ) == HubActionResult::AlreadyInParty );
    SW_EXPECT_TRUE( world.inviteToParty( firstID, firstID ) == HubActionResult::UnknownTarget );

    world.removeAvatar( firstID ); // 이끈 사람이 나가면 둘 파티가 풀린다
    SW_EXPECT_EQUAL( 1, world.getPartySize( secondID ) );
    SW_EXPECT_TRUE( world.acceptPartyInvite( thirdID, firstID ) == HubActionResult::UnknownTarget );
}

/** @brief [HubWorldTest] 단계표가 오름차순이 아니거나 놓을 것이 경계 밖이면 허브를 세우지 않는다. */
SW_TEST_CASE( HubWorldTest, InvalidRulesAreRefused )
{
    HubRules descending             = makeHubTestRules();
    descending._listLevelExperience = { 100, 90 };
    HubWorld world;
    SW_EXPECT_LOG( "Hub.LevelTableNotAscending" );
    SW_EXPECT_FALSE( world.initialize( descending ) );

    HubRules outside                           = makeHubTestRules();
    outside._listPlacement.back()._position._x = 500.0f;
    SW_EXPECT_LOG( "Hub.PlacementOutside" );
    SW_EXPECT_FALSE( world.initialize( outside ) );

    HubRules empty = makeHubTestRules();
    empty._listPlacement.clear();
    SW_EXPECT_LOG( "Hub.PlacementListInvalid" );
    SW_EXPECT_FALSE( world.initialize( empty ) );
}

/** @brief [HubWorldTest] 진행 바이트(칸 선언)는 같은 값으로 돌아오고, 판이 다르거나 값이 선언 범위 밖이면 쓰지도 읽지도 않는다. */
SW_TEST_CASE( HubWorldTest, ProfileBytesRoundTrip )
{
    HubProfile saved;
    saved._level       = 3;
    saved._experience  = 321;
    saved._coins       = 17;
    saved._herbCount   = 4;
    saved._defeatCount = 9;
    saved._position    = float3{ 8.0f, 0.0f, 6.0f };
    BitWriter writer;
    SW_REQUIRE_TRUE( saved.write( writer ) );
    const vector<uint8> bytes = writer.releaseBytes();
    HubProfile          loaded;
    BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_REQUIRE_TRUE( loaded.read( reader ) );
    SW_EXPECT_EQUAL( 3, loaded._level );
    SW_EXPECT_EQUAL( 321, loaded._experience );
    SW_EXPECT_EQUAL( 17, loaded._coins );
    SW_EXPECT_EQUAL( 4, loaded._herbCount );
    SW_EXPECT_EQUAL( 9, loaded._defeatCount );
    SW_EXPECT_NEAR( 6.0, loaded._position._z, 1e-6 );

    HubProfile negative;
    negative._coins = -1;
    BitWriter badWriter;
    SW_EXPECT_FALSE( negative.write( badWriter ) ); // `Min = 0` 밖 — 저장하지 않는다

    // 레벨 0(`Min = 1` 밖)을 실은 바이트 — 깨진 레코드를 읽는 경우를 만든다. 판 뒤 칸은 가변 길이 부호 정수 다섯과 실수 셋이다.
    BitWriter corrupt;
    corrupt.writeVarUint( HubProfile::kFormatVersion );
    corrupt.writeVarInt( 0 );
    for ( int32 index = 0; index < 4; ++index )
        corrupt.writeVarInt( 0 );
    for ( int32 index = 0; index < 3; ++index )
        corrupt.writeFloat( 0.0f );
    const vector<uint8> badBytes = corrupt.releaseBytes();
    BitReader           badReader( badBytes.data(), static_cast<int32>( badBytes.size() ) );
    HubProfile          untouched;
    untouched._coins = 5;
    SW_EXPECT_FALSE( untouched.read( badReader ) );
    SW_EXPECT_EQUAL( 5, untouched._coins ); // 거절하면 값을 바꾸지 않는다

    BitWriter otherVersion;
    otherVersion.writeVarUint( HubProfile::kFormatVersion + 1 );
    const vector<uint8> otherBytes = otherVersion.releaseBytes();
    BitReader           otherReader( otherBytes.data(), static_cast<int32>( otherBytes.size() ) );
    SW_EXPECT_FALSE( untouched.read( otherReader ) );
}
