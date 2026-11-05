#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleDrop.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleZone.h"

#include <limits>

// BattleRoyaleZoneTest — 자기장: 기다림 → 줄어듦 → 다음 단계의 시간 흐름과 보간, 한 번에 긴 시간, 다음 원이 지금 원 · 맵 안에 드는지, 지형 콜백, 경계 피해, 왕복.
// 결함: 네트워크 · 세이브로 받은 음수 남은 시간 · 음수 반지름 · 없는 단계를 받아들였다.
// BattleRoyaleDropTest — 비행 경로와 낙하: 맵 경계에서 자른 경로, 뛰어내리는 창, 낙하 단계 경계, 착지 예측과 실제의 일치(결함은 없었다). 키트 GF_BattleRoyale(nogpu).

namespace
{
    constexpr float32 kZoneTolerance = 1.0e-2f;
    constexpr float32 kMapSize       = 1000.0f;

    class DenyAllTerrain final : public sw::IBrZoneTerrain
    {
    public:
        bool isZoneCenterAllowed( const sw::float2& ) const override { return false; }
    };

    class RightSideTerrain final : public sw::IBrZoneTerrain
    {
    public:
        bool isZoneCenterAllowed( const sw::float2& position ) const override { return position._x > 500.0f; }
    };

    struct BattleRoyaleZoneTestUtil
    {
        static sw::BattleRoyaleZoneDef makeSettings( int32 phaseCount, float32 wait = 2.0f, float32 shrink = 2.0f )
        {
            sw::BattleRoyaleZoneDef settings;
            settings._startRadius = 400.0f;
            for ( int32 phaseIndex = 0; phaseIndex < phaseCount; ++phaseIndex )
            {
                sw::BattleRoyaleZonePhaseDef phase;
                phase._waitTime        = wait;
                phase._shrinkTime      = shrink;
                phase._radiusRatio     = 0.5f;
                phase._damagePerSecond = 3.0f + static_cast<float32>( phaseIndex );
                settings._listPhase.push_back( phase );
            }
            return settings;
        }

        static float32 computeDistance( const sw::float2& lhs, const sw::float2& rhs ) { return sw::float2::getDistance( lhs, rhs ); }

        /** @brief 넷 코덱과 같은 모양의 비트를 @p target 에 읽고 결과를 돌려줍니다. */
        static bool readCrafted( sw::BattleRoyaleZone& target, int32 phase, sw::BattleRoyaleZoneStage stage, float32 remaining, float32 centerX, float32 radius, float32 fromRadius, float32 nextRadius )
        {
            sw::BitWriter writer;
            const bool    bHead = writer.writeInt( phase, 0, 1024 ) && writer.writeInt( static_cast<int32>( stage ), 0, 2 );
            if ( bHead == false )
                return false;
            writer.writeFloat( remaining );
            writer.writeFloat( centerX );
            writer.writeFloat( 500.0f );
            writer.writeFloat( radius );
            writer.writeFloat( 500.0f );
            writer.writeFloat( 500.0f );
            writer.writeFloat( fromRadius );
            writer.writeFloat( 500.0f );
            writer.writeFloat( 500.0f );
            writer.writeFloat( nextRadius );
            const sw::vector<uint8>& bytes = writer.getBytes();
            sw::BitReader            reader( bytes.data(), static_cast<int32>( bytes.size() ) );
            return target.readState( reader );
        }
    };
} // namespace

/**
 * @brief [BattleRoyaleZoneTest] 맵 크기가 1 미만이면 거절한다. 단계가 없으면 맵을 덮는 원에서 바로 마지막이고 피해가 없다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, InitializeHandlesBadMapAndNoPhases )
{
    SW_EXPECT_LOG( "BattleRoyaleZone.BattleRoyaleZoneRefusedMapSize" );
    sw::BattleRoyaleZone zone;
    SW_EXPECT_FALSE( zone.initialize( sw::BattleRoyaleZoneDef{}, 0.5f, 1, nullptr ) );

    SW_REQUIRE_TRUE( zone.initialize( sw::BattleRoyaleZoneDef{}, kMapSize, 1, nullptr ) );
    SW_EXPECT_TRUE( zone.getStage() == sw::BattleRoyaleZoneStage::Final );
    SW_EXPECT_NEAR( 500.0f * 1.41421356f, zone.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( 0.0f, zone.computeDamagePerSecond( sw::float2{ -5000.0f, 0.0f } ), kZoneTolerance );
    zone.update( 100.0f );
    SW_EXPECT_TRUE( zone.getStage() == sw::BattleRoyaleZoneStage::Final );
}

/**
 * @brief [BattleRoyaleZoneTest] 기다림 → 줄어듦 → 다음 단계 순서로 흐르고, 줄어드는 동안 선형 보간하며, 알림이 순서대로 나온다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, StagesFlowAndInterpolate )
{
    sw::BattleRoyaleZone zone;
    SW_REQUIRE_TRUE( zone.initialize( BattleRoyaleZoneTestUtil::makeSettings( 1 ), kMapSize, 7, nullptr ) );
    SW_EXPECT_TRUE( zone.getStage() == sw::BattleRoyaleZoneStage::Waiting );
    SW_EXPECT_NEAR( 500.0f, zone.getCenter()._x, kZoneTolerance );
    SW_EXPECT_NEAR( 400.0f, zone.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( 200.0f, zone.getNextRadius(), kZoneTolerance );
    const sw::float2 nextCenter = zone.getNextCenter();

    zone.update( 0.0f );
    zone.update( -5.0f );
    SW_EXPECT_NEAR( 2.0f, zone.getStageRemaining(), kZoneTolerance );

    zone.update( 1.0f );
    SW_EXPECT_NEAR( 1.0f, zone.getStageRemaining(), kZoneTolerance );
    zone.update( 1.0f );
    SW_EXPECT_TRUE( zone.getStage() == sw::BattleRoyaleZoneStage::Shrinking );
    SW_EXPECT_NEAR( 2.0f, zone.getStageRemaining(), kZoneTolerance );
    SW_EXPECT_NEAR( 400.0f, zone.getRadius(), kZoneTolerance );

    zone.update( 1.0f );
    SW_EXPECT_NEAR( 300.0f, zone.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( ( 500.0f + nextCenter._x ) * 0.5f, zone.getCenter()._x, kZoneTolerance );

    zone.update( 1.0f );
    SW_EXPECT_TRUE( zone.getStage() == sw::BattleRoyaleZoneStage::Final );
    SW_EXPECT_NEAR( 200.0f, zone.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( nextCenter._x, zone.getCenter()._x, kZoneTolerance );
    SW_EXPECT_EQUAL( 1, zone.getPhaseIndex() );

    sw::vector<sw::BattleRoyaleZoneEvent> listEvent;
    zone.drainEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 4 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == sw::BattleRoyaleZoneEvent::Kind::NextZoneRevealed );
    SW_EXPECT_NEAR( 200.0f, listEvent[0]._radius, kZoneTolerance );
    SW_EXPECT_TRUE( listEvent[1]._kind == sw::BattleRoyaleZoneEvent::Kind::ShrinkStarted );
    SW_EXPECT_TRUE( listEvent[2]._kind == sw::BattleRoyaleZoneEvent::Kind::ShrinkFinished );
    SW_EXPECT_TRUE( listEvent[3]._kind == sw::BattleRoyaleZoneEvent::Kind::FinalZone );
    SW_EXPECT_EQUAL( 1, listEvent[3]._phase );
}

/**
 * @brief [BattleRoyaleZoneTest] 한 번에 긴 시간을 넘겨도 나눠 넘긴 것과 같은 곳에 닿고, 기다림 · 줄어듦이 0 인 단계도 지나간다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, LongStepsMatchShortSteps )
{
    const sw::BattleRoyaleZoneDef settings = BattleRoyaleZoneTestUtil::makeSettings( 3 );
    sw::BattleRoyaleZone          whole;
    SW_REQUIRE_TRUE( whole.initialize( settings, kMapSize, 11, nullptr ) );
    whole.update( 100.0f );
    sw::BattleRoyaleZone stepped;
    SW_REQUIRE_TRUE( stepped.initialize( settings, kMapSize, 11, nullptr ) );
    for ( int32 step = 0; step < 16; ++step )
    {
        stepped.update( 1.0f );
    }
    SW_EXPECT_TRUE( whole.getStage() == sw::BattleRoyaleZoneStage::Final );
    SW_EXPECT_TRUE( stepped.getStage() == sw::BattleRoyaleZoneStage::Final );
    SW_EXPECT_NEAR( 50.0f, whole.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( whole.getRadius(), stepped.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( whole.getCenter()._x, stepped.getCenter()._x, kZoneTolerance );
    SW_EXPECT_NEAR( whole.getCenter()._y, stepped.getCenter()._y, kZoneTolerance );

    sw::BattleRoyaleZone instant;
    SW_REQUIRE_TRUE( instant.initialize( BattleRoyaleZoneTestUtil::makeSettings( 2, 0.0f, 0.0f ), kMapSize, 3, nullptr ) );
    instant.update( 0.0625f );
    SW_EXPECT_TRUE( instant.getStage() == sw::BattleRoyaleZoneStage::Final );
    SW_EXPECT_NEAR( 100.0f, instant.getRadius(), kZoneTolerance );
}

/**
 * @brief [BattleRoyaleZoneTest] 어떤 시드에서도 다음 원은 지금 원 안에 통째로 들고 맵을 벗어나지 않는다. 같은 시드는 같은 원이다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, NextCircleStaysInsideCurrentAndMap )
{
    const sw::BattleRoyaleZoneDef settings = BattleRoyaleZoneTestUtil::makeSettings( 4 );
    for ( uint32 seed = 1; seed <= 20; ++seed )
    {
        sw::BattleRoyaleZone zone;
        SW_REQUIRE_TRUE( zone.initialize( settings, kMapSize, seed, nullptr ) );
        sw::BattleRoyaleZone twin;
        SW_REQUIRE_TRUE( twin.initialize( settings, kMapSize, seed, nullptr ) );
        for ( int32 phase = 0; phase < 4; ++phase )
        {
            const float32 distance = BattleRoyaleZoneTestUtil::computeDistance( zone.getCenter(), zone.getNextCenter() );
            SW_EXPECT_TRUE( distance + zone.getNextRadius() <= zone.getRadius() + kZoneTolerance );
            SW_EXPECT_TRUE( zone.getNextCenter()._x - zone.getNextRadius() >= -kZoneTolerance );
            SW_EXPECT_TRUE( zone.getNextCenter()._y - zone.getNextRadius() >= -kZoneTolerance );
            SW_EXPECT_TRUE( zone.getNextCenter()._x + zone.getNextRadius() <= kMapSize + kZoneTolerance );
            SW_EXPECT_TRUE( zone.getNextCenter()._y + zone.getNextRadius() <= kMapSize + kZoneTolerance );
            SW_EXPECT_NEAR( zone.getNextCenter()._x, twin.getNextCenter()._x, kZoneTolerance );
            zone.update( 4.0f );
            twin.update( 4.0f );
        }
    }
}

/**
 * @brief [BattleRoyaleZoneTest] 지형이 모두 막으면 중심이 그대로이고 보급 위치는 못 찾았다고 답한다. 일부만 허락하면 그 안에서 고른다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, TerrainDecidesTheCenter )
{
    const DenyAllTerrain denyAll{};
    sw::BattleRoyaleZone blocked;
    SW_REQUIRE_TRUE( blocked.initialize( BattleRoyaleZoneTestUtil::makeSettings( 1 ), kMapSize, 5, &denyAll ) );
    SW_EXPECT_NEAR( 500.0f, blocked.getNextCenter()._x, kZoneTolerance );
    SW_EXPECT_NEAR( 500.0f, blocked.getNextCenter()._y, kZoneTolerance );
    sw::GameRandom random{ 9u };
    sw::float2     point{ 1.0f, 1.0f };
    SW_EXPECT_FALSE( blocked.pickPointInside( random, point ) );
    SW_EXPECT_NEAR( 500.0f, point._x, kZoneTolerance );

    const RightSideTerrain rightSide{};
    for ( uint32 seed = 1; seed <= 10; ++seed )
    {
        sw::BattleRoyaleZone zone;
        SW_REQUIRE_TRUE( zone.initialize( BattleRoyaleZoneTestUtil::makeSettings( 1 ), kMapSize, seed, &rightSide ) );
        SW_EXPECT_TRUE( zone.getNextCenter()._x > 500.0f );
        SW_EXPECT_TRUE( zone.pickPointInside( random, point ) );
        SW_EXPECT_TRUE( point._x > 500.0f );
        SW_EXPECT_TRUE( zone.isInside( point ) );
    }
}

/**
 * @brief [BattleRoyaleZoneTest] 원 위는 안이라 피해가 없고, 밖에서는 지금 단계의 피해를 받는다. 다 줄어든 뒤에는 마지막 단계의 피해가 이어진다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, DamageAppliesOutsideOnly )
{
    sw::BattleRoyaleZone zone;
    SW_REQUIRE_TRUE( zone.initialize( BattleRoyaleZoneTestUtil::makeSettings( 2 ), kMapSize, 7, nullptr ) );
    const sw::float2 onEdge{ 900.0f, 500.0f };
    const sw::float2 beyond{ 901.0f, 500.0f };
    SW_EXPECT_TRUE( zone.isInside( onEdge ) );
    SW_EXPECT_NEAR( 0.0f, zone.computeDamagePerSecond( onEdge ), kZoneTolerance );
    SW_EXPECT_NEAR( 0.0f, zone.computeDistanceOutside( onEdge ), kZoneTolerance );
    SW_EXPECT_FALSE( zone.isInside( beyond ) );
    SW_EXPECT_NEAR( 3.0f, zone.computeDamagePerSecond( beyond ), kZoneTolerance );
    SW_EXPECT_NEAR( 1.0f, zone.computeDistanceOutside( beyond ), kZoneTolerance );

    zone.update( 4.0f ); // 둘째 단계
    const sw::float2 farAway{ -5000.0f, -5000.0f };
    SW_EXPECT_NEAR( 4.0f, zone.computeDamagePerSecond( farAway ), kZoneTolerance );
    zone.update( 100.0f );
    SW_EXPECT_TRUE( zone.getStage() == sw::BattleRoyaleZoneStage::Final );
    SW_EXPECT_NEAR( 4.0f, zone.computeDamagePerSecond( farAway ), kZoneTolerance );
}

/**
 * @brief [BattleRoyaleZoneTest] 세이브로 쓰고 읽으면 난수까지 이어져 다음 원도 원본과 같고, 읽으면 알림을 비운다. 잘린 바이트는 거절한다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, ArchiveRoundTripResumes )
{
    const sw::BattleRoyaleZoneDef settings = BattleRoyaleZoneTestUtil::makeSettings( 3 );
    sw::BattleRoyaleZone          original;
    SW_REQUIRE_TRUE( original.initialize( settings, kMapSize, 21, nullptr ) );
    original.update( 3.0f ); // 첫 단계가 줄어드는 중

    sw::Archive written;
    original.writeState( written );
    sw::BattleRoyaleZone restored;
    SW_REQUIRE_TRUE( restored.initialize( settings, kMapSize, 999, nullptr ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    sw::vector<sw::BattleRoyaleZoneEvent> listEvent;
    restored.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
    SW_EXPECT_TRUE( restored.getStage() == sw::BattleRoyaleZoneStage::Shrinking );
    SW_EXPECT_NEAR( original.getRadius(), restored.getRadius(), kZoneTolerance );

    for ( int32 step = 0; step < 12; ++step )
    {
        original.update( 1.0f );
        restored.update( 1.0f );
        SW_EXPECT_NEAR( original.getNextCenter()._x, restored.getNextCenter()._x, kZoneTolerance );
        SW_EXPECT_NEAR( original.getRadius(), restored.getRadius(), kZoneTolerance );
    }

    sw::BattleRoyaleZone truncatedTarget;
    SW_REQUIRE_TRUE( truncatedTarget.initialize( settings, kMapSize, 1, nullptr ) );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncatedTarget.readState( truncated ) );
    SW_EXPECT_NEAR( 400.0f, truncatedTarget.getRadius(), kZoneTolerance );
}

/**
 * @brief [BattleRoyaleZoneTest] 결함: 받은 음수 남은 시간(시간을 늘린다) · 음수 반지름(모두 원 밖) · 없는 단계를 받아들였다. 무한 · NaN 과 함께 거절하고 상태를 그대로 둔다
 */
SW_TEST_CASE( BattleRoyaleZoneTest, CodecRejectsOutOfRangeValues )
{
    sw::BattleRoyaleZone zone;
    SW_REQUIRE_TRUE( zone.initialize( BattleRoyaleZoneTestUtil::makeSettings( 1 ), kMapSize, 2, nullptr ) );
    using Stage        = sw::BattleRoyaleZoneStage;
    const float32 nan  = std::numeric_limits<float32>::quiet_NaN();
    const float32 huge = std::numeric_limits<float32>::infinity();
    const auto    read = [&zone]( int32 phase, Stage stage, float32 remaining, float32 centerX, float32 radius, float32 fromRadius, float32 nextRadius )
    { return BattleRoyaleZoneTestUtil::readCrafted( zone, phase, stage, remaining, centerX, radius, fromRadius, nextRadius ); };

    SW_EXPECT_FALSE( read( 0, Stage::Waiting, -1.0f, 500.0f, 100.0f, 100.0f, 50.0f ) );
    SW_EXPECT_FALSE( read( 0, Stage::Waiting, 1.0f, 500.0f, -1.0f, 100.0f, 50.0f ) );
    SW_EXPECT_FALSE( read( 0, Stage::Waiting, 1.0f, 500.0f, 100.0f, -1.0f, 50.0f ) );
    SW_EXPECT_FALSE( read( 0, Stage::Waiting, 1.0f, 500.0f, 100.0f, 100.0f, -1.0f ) );
    SW_EXPECT_FALSE( read( 1, Stage::Waiting, 1.0f, 500.0f, 100.0f, 100.0f, 50.0f ) ); // 정의가 없는 단계를 기다린다
    SW_EXPECT_FALSE( read( 2, Stage::Final, 0.0f, 500.0f, 100.0f, 100.0f, 50.0f ) );
    SW_EXPECT_FALSE( read( 0, Stage::Waiting, nan, 500.0f, 100.0f, 100.0f, 50.0f ) );
    SW_EXPECT_FALSE( read( 0, Stage::Waiting, 1.0f, huge, 100.0f, 100.0f, 50.0f ) );
    SW_EXPECT_FALSE( read( 2000, Stage::Final, 0.0f, 500.0f, 100.0f, 100.0f, 50.0f ) ); // 코덱 범위 밖
    SW_EXPECT_NEAR( 400.0f, zone.getRadius(), kZoneTolerance );
    SW_EXPECT_EQUAL( 0, zone.getPhaseIndex() );

    SW_EXPECT_TRUE( read( 1, Stage::Final, 0.0f, 450.0f, 100.0f, 100.0f, 100.0f ) ); // 다 줄어든 마지막 단계
    SW_EXPECT_NEAR( 100.0f, zone.getRadius(), kZoneTolerance );
    SW_EXPECT_NEAR( 450.0f, zone.getCenter()._x, kZoneTolerance );
}

/**
 * @brief [BattleRoyaleDropTest] 범위 밖 비행 설정 · 맵 크기는 거절하고 결과를 건드리지 않는다
 */
SW_TEST_CASE( BattleRoyaleDropTest, MakeRandomRefusesBadSettings )
{
    SW_EXPECT_LOG( "BattleRoyaleDrop.BattleRoyaleFlightPathRefusedFlightSettings" );
    SW_EXPECT_LOG( "BattleRoyaleDrop.BattleRoyaleFlightPathRefusedMapSize" );
    sw::BattleRoyaleFlightPath path;
    path._speed = 77.0f;

    sw::BattleRoyaleFlightDef reversed;
    reversed._jumpStartRatio = 0.5f;
    reversed._jumpEndRatio   = 0.25f;
    SW_EXPECT_FALSE( sw::BattleRoyaleFlightPath::makeRandom( reversed, kMapSize, 1, path ) );
    SW_EXPECT_FALSE( sw::BattleRoyaleFlightPath::makeRandom( sw::BattleRoyaleFlightDef{}, 0.5f, 1, path ) );
    SW_EXPECT_NEAR( 77.0f, path._speed, kZoneTolerance );
}

/**
 * @brief [BattleRoyaleDropTest] 어떤 시드에서도 경로는 맵 경계의 두 점을 잇는 직선이고, 같은 시드는 같은 경로다
 */
SW_TEST_CASE( BattleRoyaleDropTest, RandomPathsCrossTheMap )
{
    const sw::BattleRoyaleFlightDef settings{};
    for ( uint32 seed = 1; seed <= 50; ++seed )
    {
        sw::BattleRoyaleFlightPath path;
        SW_REQUIRE_TRUE( sw::BattleRoyaleFlightPath::makeRandom( settings, kMapSize, seed, path ) );
        const sw::float2 arrEnd[] = { path._start, path._end };
        for ( const sw::float2& point : arrEnd )
        {
            SW_EXPECT_TRUE( point._x >= -kZoneTolerance && point._x <= kMapSize + kZoneTolerance );
            SW_EXPECT_TRUE( point._y >= -kZoneTolerance && point._y <= kMapSize + kZoneTolerance );
            const float32 toEdge = sw::MathUtil::min( sw::MathUtil::min( point._x, kMapSize - point._x ), sw::MathUtil::min( point._y, kMapSize - point._y ) );
            SW_EXPECT_TRUE( sw::MathUtil::abs( toEdge ) <= 0.1f );
        }
        SW_EXPECT_TRUE( path.computeLength() > 1.0f );
        sw::BattleRoyaleFlightPath twin;
        SW_REQUIRE_TRUE( sw::BattleRoyaleFlightPath::makeRandom( settings, kMapSize, seed, twin ) );
        SW_EXPECT_NEAR( path._start._x, twin._start._x, kZoneTolerance );
        SW_EXPECT_NEAR( path._end._y, twin._end._y, kZoneTolerance );
    }
}

/**
 * @brief [BattleRoyaleDropTest] 위치는 시간 밖에서 양 끝에 머물고, 뛰어내리는 창은 양 끝을 포함한다. 길이 0 인 경로도 깨지지 않는다
 */
SW_TEST_CASE( BattleRoyaleDropTest, PathTimingBoundaries )
{
    sw::BattleRoyaleFlightPath path;
    path._start          = sw::float2{ 0.0f, 0.0f };
    path._end            = sw::float2{ 100.0f, 0.0f };
    path._speed          = 10.0f;
    path._jumpStartRatio = 0.25f;
    path._jumpEndRatio   = 0.75f;
    SW_EXPECT_NEAR( 100.0f, path.computeLength(), kZoneTolerance );
    SW_EXPECT_NEAR( 10.0f, path.computeDuration(), kZoneTolerance );
    SW_EXPECT_NEAR( 50.0f, path.computePosition( 5.0f )._x, kZoneTolerance );
    SW_EXPECT_NEAR( 0.0f, path.computePosition( -3.0f )._x, kZoneTolerance );
    SW_EXPECT_NEAR( 100.0f, path.computePosition( 30.0f )._x, kZoneTolerance );
    SW_EXPECT_FALSE( path.canJump( 2.25f ) );
    SW_EXPECT_TRUE( path.canJump( 2.5f ) );
    SW_EXPECT_TRUE( path.canJump( 7.5f ) );
    SW_EXPECT_FALSE( path.canJump( 7.75f ) );

    sw::BattleRoyaleFlightPath still;
    still._start = sw::float2{ 3.0f, 4.0f };
    still._end   = still._start;
    SW_EXPECT_NEAR( 0.0f, still.computeDuration(), kZoneTolerance );
    SW_EXPECT_NEAR( 3.0f, still.computePosition( 1.0f )._x, kZoneTolerance );
}

/**
 * @brief [BattleRoyaleDropTest] 시작 높이가 단계를 정한다: 땅 이하는 착지, 자동 펼침 높이 이하는 낙하산, 그 위는 자유 낙하다. 손으로는 자유 낙하 중에 한 번만 편다
 */
SW_TEST_CASE( BattleRoyaleDropTest, SkydiverStartsInTheRightStage )
{
    const sw::BattleRoyaleFallDef settings{};
    sw::BattleRoyaleSkydiver      diver;
    SW_EXPECT_TRUE( diver.isLanded() ); // 초기화 전

    diver.initialize( settings, sw::float3{ 0.0f, 149.0f, 0.0f }, 50.0f ); // 땅 위 99
    SW_EXPECT_TRUE( diver.getStage() == sw::BattleRoyaleFallStage::Parachute );
    SW_EXPECT_FALSE( diver.tryOpenParachute() );
    diver.initialize( settings, sw::float3{ 0.0f, 150.0f, 0.0f }, 50.0f ); // 정확히 100
    SW_EXPECT_TRUE( diver.getStage() == sw::BattleRoyaleFallStage::Parachute );
    diver.initialize( settings, sw::float3{ 0.0f, 151.0f, 0.0f }, 50.0f );
    SW_EXPECT_TRUE( diver.getStage() == sw::BattleRoyaleFallStage::FreeFall );
    SW_EXPECT_TRUE( diver.tryOpenParachute() );
    SW_EXPECT_FALSE( diver.tryOpenParachute() );
    diver.initialize( settings, sw::float3{ 0.0f, 50.0f, 0.0f }, 50.0f );
    SW_EXPECT_TRUE( diver.isLanded() );
    diver.initialize( settings, sw::float3{ 0.0f, 10.0f, 0.0f }, 50.0f );
    SW_EXPECT_TRUE( diver.isLanded() );
    diver.update( 1.0f, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.0f, diver.getPosition()._x, kZoneTolerance );
    SW_EXPECT_NEAR( 10.0f, diver.predictLanding( sw::float2{ 1.0f, 0.0f } )._y, kZoneTolerance ); // 내린 사람의 예측은 제자리다
}

/**
 * @brief [BattleRoyaleDropTest] 자유 낙하 → 자동 펼침 → 착지로 이어지고 예측한 곳에 내린다. 긴 걸음이 경계를 넘어도, 작은 걸음을 쌓아도 같다. 조종은 길이 1 로 자른다
 */
SW_TEST_CASE( BattleRoyaleDropTest, FallMatchesPrediction )
{
    const sw::BattleRoyaleFallDef settings{}; // 자유 50 · 수평 20, 낙하산 5 · 수평 8, 자동 펼침 100
    const sw::float2              right{ 1.0f, 0.0f };

    sw::BattleRoyaleSkydiver diver;
    diver.initialize( settings, sw::float3{ 0.0f, 600.0f, 0.0f }, 0.0f );
    diver.update( 0.0f, right );
    diver.update( -3.0f, right );
    SW_EXPECT_NEAR( 600.0f, diver.getPosition()._y, kZoneTolerance );
    diver.update( 5.0f, right );
    SW_EXPECT_NEAR( 350.0f, diver.getPosition()._y, kZoneTolerance );
    SW_EXPECT_NEAR( 100.0f, diver.getPosition()._x, kZoneTolerance );
    SW_EXPECT_NEAR( 360.0f, diver.predictLanding( right )._x, kZoneTolerance );
    SW_EXPECT_NEAR( 0.0f, diver.predictLanding( right )._y, kZoneTolerance );

    diver.update( 5.0f, right ); // 정확히 자동 펼침 높이
    SW_EXPECT_TRUE( diver.getStage() == sw::BattleRoyaleFallStage::Parachute );
    diver.update( 1000.0f, right );
    SW_EXPECT_TRUE( diver.isLanded() );
    SW_EXPECT_NEAR( 360.0f, diver.getPosition()._x, kZoneTolerance );
    SW_EXPECT_NEAR( 0.0f, diver.getPosition()._y, kZoneTolerance );

    sw::BattleRoyaleSkydiver clamped;
    clamped.initialize( settings, sw::float3{ 0.0f, 600.0f, 0.0f }, 0.0f );
    clamped.update( 1.0f, sw::float2{ 3.0f, 4.0f } );
    SW_EXPECT_NEAR( 12.0f, clamped.getPosition()._x, kZoneTolerance );
    SW_EXPECT_NEAR( 16.0f, clamped.getPosition()._z, kZoneTolerance );

    sw::BattleRoyaleSkydiver opened;
    opened.initialize( settings, sw::float3{ 0.0f, 600.0f, 0.0f }, 0.0f );
    SW_REQUIRE_TRUE( opened.tryOpenParachute() );
    SW_EXPECT_NEAR( 960.0f, opened.predictLanding( right )._x, kZoneTolerance ); // 120 초 × 8

    const sw::float2         half{ 0.0f, 0.5f };
    sw::BattleRoyaleSkydiver stepped;
    stepped.initialize( settings, sw::float3{ 5.0f, 300.0f, 7.0f }, 20.0f );
    const sw::float3 predicted = stepped.predictLanding( half );
    for ( int32 frame = 0; frame < 4000 && stepped.isLanded() == false; ++frame )
    {
        stepped.update( 1.0f / 60.0f, half );
    }
    SW_EXPECT_TRUE( stepped.isLanded() );
    SW_EXPECT_NEAR( predicted._x, stepped.getPosition()._x, kZoneTolerance );
    SW_EXPECT_NEAR( predicted._z, stepped.getPosition()._z, 0.05f );
    SW_EXPECT_NEAR( 20.0f, stepped.getPosition()._y, kZoneTolerance );
}
