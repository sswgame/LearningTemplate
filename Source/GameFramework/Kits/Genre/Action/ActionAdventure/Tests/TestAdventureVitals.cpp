#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"

#include <cstring>
#include <limits>

// AdventureVitalsTest — 몸 상태: 4분의 1 하트 피해 · 회복 경계, 하트 조각 · 그릇 상한, 두 배 마법, 스태미나 바닥의 행동별 결과와 탈진 회복, 상태 왕복.
// 결함: 체력 · 마력 · 지구력이 최대를 넘는 상태 바이트를 받았다 · 체력 알림을 꺼낼 길이 없어 끝없이 쌓였다. 키트 GF_ActionAdventure(nogpu).

namespace
{
    constexpr float32 kVitalsTolerance = 1.0e-3f;

    struct AdventureVitalsTestUtil
    {
        /** @brief 바이트의 @p offset 을 @p value 로 덮어쓴 상태를 @p target 에 읽고 그 결과를 돌려줍니다. */
        template <typename TValue>
        static bool readPatched( const sw::Archive& written, size_t offset, const TValue& value, sw::AdventureVitals& target )
        {
            sw::vector<uint8> listByte( written.getData(), written.getData() + static_cast<size_t>( written.getSize() ) );
            std::memcpy( listByte.data() + offset, &value, sizeof( TValue ) );
            sw::Archive reading( listByte.data(), listByte.size() );
            return target.readState( reading );
        }
    };
} // namespace

/**
 * @brief [AdventureVitalsTest] 범위 밖 설정은 맞추지 않고 거절하며 상태를 그대로 둔다(하트 시작 수는 최대 이하, 탈진 회복 수준은 스태미나 최대 이하)
 */
SW_TEST_CASE( AdventureVitalsTest, RefusesOutOfRangeSettingsInsteadOfClamping )
{
    SW_EXPECT_LOG( "AdventureVitals.AdventureVitalsRefusedSettings" );
    sw::AdventureVitals vitals;
    SW_EXPECT_EQUAL( 3, vitals.getHeartCount() );
    SW_EXPECT_EQUAL( 12, vitals.computeHealthQuarters() );

    sw::AdventureVitalsSettings moreThanMax;
    moreThanMax._startHeartCount = 21;
    SW_EXPECT_FALSE( vitals.initialize( moreThanMax ) );
    sw::AdventureVitalsSettings noPieces;
    noPieces._piecesPerHeart = 0;
    SW_EXPECT_FALSE( vitals.initialize( noPieces ) );
    sw::AdventureVitalsSettings recoverAboveMax;
    recoverAboveMax._staminaRecoverLevel = 101.0f;
    SW_EXPECT_FALSE( vitals.initialize( recoverAboveMax ) );
    SW_EXPECT_EQUAL( 3, vitals.getHeartCount() );
    SW_EXPECT_EQUAL( 4, vitals.getSettings()._piecesPerHeart );

    sw::AdventureVitalsSettings valid;
    valid._startHeartCount = 5;
    SW_REQUIRE_TRUE( vitals.initialize( valid ) );
    SW_EXPECT_EQUAL( 20, vitals.getMaxHealthQuarters() );
    SW_EXPECT_EQUAL( 20, vitals.computeHealthQuarters() );
}

/**
 * @brief [AdventureVitalsTest] 0 · 음수 피해와 회복은 무시하고, 회복은 빈 만큼만 차며, 치명타는 그 한 번만 "죽었다"고 알린다. 죽으면 회복되지 않는다
 */
SW_TEST_CASE( AdventureVitalsTest, DamageAndHealRespectBounds )
{
    sw::AdventureVitals vitals;
    SW_EXPECT_FALSE( vitals.applyDamage( 0 ) );
    SW_EXPECT_FALSE( vitals.applyDamage( -5 ) );
    SW_EXPECT_EQUAL( 12, vitals.computeHealthQuarters() );

    SW_EXPECT_FALSE( vitals.applyDamage( 3 ) );
    SW_EXPECT_EQUAL( 9, vitals.computeHealthQuarters() );
    SW_EXPECT_EQUAL( 0, vitals.heal( 0 ) );
    SW_EXPECT_EQUAL( 0, vitals.heal( -1 ) );
    SW_EXPECT_EQUAL( 3, vitals.heal( 100 ) );
    SW_EXPECT_EQUAL( 0, vitals.heal( 1 ) );

    SW_EXPECT_FALSE( vitals.applyDamage( 11 ) );
    SW_EXPECT_FALSE( vitals.isDead() );
    SW_EXPECT_TRUE( vitals.applyDamage( 1 ) );
    SW_EXPECT_TRUE( vitals.isDead() );
    SW_EXPECT_FALSE( vitals.applyDamage( 1 ) );
    SW_EXPECT_EQUAL( 0, vitals.heal( 4 ) );
}

/**
 * @brief [AdventureVitalsTest] 조각이 다 모이면 그릇이 되어 체력이 가득 차고, 최대에 닿으면 조각도 그릇도 받지 않는다. 죽은 몸은 그릇으로 살아나지 않는다
 */
SW_TEST_CASE( AdventureVitalsTest, HeartPiecesBecomeContainersUpToTheLimit )
{
    sw::AdventureVitalsSettings settings;
    settings._startHeartCount = 2;
    settings._maxHeartCount   = 3;
    settings._piecesPerHeart  = 2;
    sw::AdventureVitals vitals;
    SW_REQUIRE_TRUE( vitals.initialize( settings ) );
    SW_EXPECT_FALSE( vitals.applyDamage( 5 ) );

    SW_EXPECT_FALSE( vitals.addHeartPiece() );
    SW_EXPECT_EQUAL( 1, vitals.getHeartPieceCount() );
    SW_EXPECT_EQUAL( 3, vitals.computeHealthQuarters() );
    SW_EXPECT_TRUE( vitals.addHeartPiece() );
    SW_EXPECT_EQUAL( 0, vitals.getHeartPieceCount() );
    SW_EXPECT_EQUAL( 3, vitals.getHeartCount() );
    SW_EXPECT_EQUAL( 12, vitals.computeHealthQuarters() );

    SW_EXPECT_FALSE( vitals.addHeartPiece() );
    SW_EXPECT_EQUAL( 0, vitals.getHeartPieceCount() ); // 최대면 쌓이지 않는다
    SW_EXPECT_FALSE( vitals.addHeartContainer() );
    SW_EXPECT_EQUAL( 3, vitals.getHeartCount() );

    sw::AdventureVitals dead;
    SW_REQUIRE_TRUE( dead.initialize( sw::AdventureVitalsSettings{} ) );
    SW_EXPECT_TRUE( dead.applyDamage( 100 ) );
    SW_EXPECT_TRUE( dead.addHeartContainer() );
    SW_EXPECT_TRUE( dead.isDead() );
    SW_EXPECT_EQUAL( 0, dead.computeHealthQuarters() );
}

/**
 * @brief [AdventureVitalsTest] 마법은 남은 만큼만 쓰고 저절로 차지 않으며, 두 배 마법은 한 번만 받고 그만큼 함께 찬다
 */
SW_TEST_CASE( AdventureVitalsTest, MagicSpendsAndUpgradesOnce )
{
    sw::AdventureVitals vitals;
    SW_EXPECT_FALSE( vitals.trySpendMagic( 49.0f ) );
    SW_EXPECT_FALSE( vitals.trySpendMagic( -1.0f ) );
    SW_EXPECT_TRUE( vitals.trySpendMagic( 48.0f ) );
    SW_EXPECT_NEAR( 0.0f, vitals.getMagic().getValue(), kVitalsTolerance );
    (void)vitals.updateStamina( sw::AdventureStaminaAction::Idle, 100.0f );
    SW_EXPECT_NEAR( 0.0f, vitals.getMagic().getValue(), kVitalsTolerance );

    SW_EXPECT_TRUE( vitals.upgradeMagic() );
    SW_EXPECT_TRUE( vitals.isMagicUpgraded() );
    SW_EXPECT_NEAR( 96.0f, vitals.getMagic().getMax(), kVitalsTolerance );
    SW_EXPECT_NEAR( 48.0f, vitals.getMagic().getValue(), kVitalsTolerance );
    SW_EXPECT_FALSE( vitals.upgradeMagic() );
    SW_EXPECT_NEAR( 96.0f, vitals.getMagic().getMax(), kVitalsTolerance );

    vitals.restoreMagic( 1000.0f );
    SW_EXPECT_NEAR( 96.0f, vitals.getMagic().getValue(), kVitalsTolerance );
}

/**
 * @brief [AdventureVitalsTest] 스태미나가 바닥나면 오르기 · 활공은 떨어지고 질주는 멈추며, 수영은 피해를 입고 다시 찬다. 쉬면 계속이고 남아 있으면 계속이다
 */
SW_TEST_CASE( AdventureVitalsTest, StaminaResultDependsOnTheAction )
{
    struct Case
    {
        sw::AdventureStaminaAction _action;
        sw::AdventureStaminaResult _result;
    };
    const Case arrCase[] = {
        { sw::AdventureStaminaAction::Climb,  sw::AdventureStaminaResult::Fall},
        { sw::AdventureStaminaAction::Glide,  sw::AdventureStaminaResult::Fall},
        {sw::AdventureStaminaAction::Sprint,  sw::AdventureStaminaResult::Stop},
        {  sw::AdventureStaminaAction::Swim, sw::AdventureStaminaResult::Drown},
    };
    for ( const Case& testCase : arrCase )
    {
        sw::AdventureVitals vitals;
        SW_EXPECT_TRUE( vitals.updateStamina( testCase._action, 1.0f ) == sw::AdventureStaminaResult::Continue );
        SW_EXPECT_TRUE( vitals.getStamina().getValue() < 100.0f );
        SW_EXPECT_TRUE( vitals.updateStamina( testCase._action, 1000.0f ) == testCase._result );
    }

    sw::AdventureVitals vitals;
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Climb, 1.0f ) == sw::AdventureStaminaResult::Continue );
    SW_EXPECT_NEAR( 90.0f, vitals.getStamina().getValue(), kVitalsTolerance );
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Sprint, 1.0f ) == sw::AdventureStaminaResult::Continue );
    SW_EXPECT_NEAR( 75.0f, vitals.getStamina().getValue(), kVitalsTolerance );
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Idle, 100.0f ) == sw::AdventureStaminaResult::Continue );
    SW_EXPECT_NEAR( 100.0f, vitals.getStamina().getValue(), kVitalsTolerance );
}

/**
 * @brief [AdventureVitalsTest] 탈진하면 회복 수준까지 다시 찰 때까지 어떤 행동도 못 쓰고, 그동안 쓰려 해도 깎이지 않는다
 */
SW_TEST_CASE( AdventureVitalsTest, ExhaustionLocksUntilRecovered )
{
    sw::AdventureVitals vitals;
    SW_REQUIRE_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Sprint, 1000.0f ) == sw::AdventureStaminaResult::Stop );
    SW_EXPECT_TRUE( vitals.getStamina().isExhausted() );
    SW_EXPECT_FALSE( vitals.trySpendStamina( 1.0f ) );

    // 지연 0.5 초 뒤 초당 40: 2 초 쉬면 60 이고 아직 잠겨 있다(회복 수준 100).
    (void)vitals.updateStamina( sw::AdventureStaminaAction::Idle, 2.0f );
    SW_EXPECT_NEAR( 60.0f, vitals.getStamina().getValue(), kVitalsTolerance );
    SW_EXPECT_TRUE( vitals.getStamina().isExhausted() );
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Climb, 0.1f ) == sw::AdventureStaminaResult::Fall );
    SW_EXPECT_NEAR( 60.0f, vitals.getStamina().getValue(), kVitalsTolerance ); // 잠겨 있으면 깎이지도 않는다

    (void)vitals.updateStamina( sw::AdventureStaminaAction::Idle, 1.0f );
    SW_EXPECT_NEAR( 100.0f, vitals.getStamina().getValue(), kVitalsTolerance );
    SW_EXPECT_FALSE( vitals.getStamina().isExhausted() );
    SW_EXPECT_TRUE( vitals.trySpendStamina( 1.0f ) );
}

/**
 * @brief [AdventureVitalsTest] 물에 빠지면 정해진 피해를 입고 스태미나가 가득 차 잠금이 풀린다. 탈진한 채 들어가도 바로 빠진다
 */
SW_TEST_CASE( AdventureVitalsTest, DrowningDamagesAndRefills )
{
    sw::AdventureVitals vitals;
    SW_REQUIRE_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Sprint, 1000.0f ) == sw::AdventureStaminaResult::Stop );
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Swim, 0.1f ) == sw::AdventureStaminaResult::Drown );
    SW_EXPECT_EQUAL( 8, vitals.computeHealthQuarters() );
    SW_EXPECT_FALSE( vitals.getStamina().isExhausted() );
    SW_EXPECT_NEAR( 100.0f, vitals.getStamina().getValue(), kVitalsTolerance );

    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Swim, 1000.0f ) == sw::AdventureStaminaResult::Drown );
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Swim, 1000.0f ) == sw::AdventureStaminaResult::Drown );
    SW_EXPECT_TRUE( vitals.isDead() );
    SW_EXPECT_TRUE( vitals.updateStamina( sw::AdventureStaminaAction::Swim, 1000.0f ) == sw::AdventureStaminaResult::Drown ); // 결과만 알리고 더 깎을 것이 없다
    SW_EXPECT_EQUAL( 0, vitals.computeHealthQuarters() );
}

/**
 * @brief [AdventureVitalsTest] 스태미나 그릇은 상한까지만 늘고 늘어난 만큼 찬다. 탈진 회복 수준은 그대로다
 */
SW_TEST_CASE( AdventureVitalsTest, StaminaVesselsStopAtTheLimit )
{
    sw::AdventureVitalsSettings settings;
    settings._staminaVessel    = 20.0f;
    settings._staminaVesselMax = 40.0f;
    sw::AdventureVitals vitals;
    SW_REQUIRE_TRUE( vitals.initialize( settings ) );
    SW_EXPECT_TRUE( vitals.addStaminaVessel() );
    SW_EXPECT_NEAR( 120.0f, vitals.getStamina().getValue(), kVitalsTolerance );
    SW_EXPECT_TRUE( vitals.addStaminaVessel() );
    SW_EXPECT_FALSE( vitals.addStaminaVessel() );
    SW_EXPECT_NEAR( 140.0f, vitals.getStamina().getMax(), kVitalsTolerance );
    SW_EXPECT_NEAR( 100.0f, vitals.getStamina().getSettings()._exhaustThreshold, kVitalsTolerance );
}

/**
 * @brief [AdventureVitalsTest] 결함: 체력 알림을 꺼낼 길이 없어 맞을 때마다 끝없이 쌓였다. 꺼내면 맞음 · 찬 양 · 죽음이 순서대로 나오고 비워진다
 */
SW_TEST_CASE( AdventureVitalsTest, HealthEventsCanBeDrained )
{
    sw::AdventureVitals vitals;
    SW_EXPECT_FALSE( vitals.applyDamage( 2 ) );
    (void)vitals.heal( 1 );
    SW_EXPECT_TRUE( vitals.applyDamage( 100 ) );

    sw::vector<sw::HealthEvent> listEvent;
    vitals.pollHealthEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 4 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._type == sw::HealthEventType::Damaged );
    SW_EXPECT_TRUE( listEvent[1]._type == sw::HealthEventType::Healed );
    SW_EXPECT_TRUE( listEvent[2]._type == sw::HealthEventType::Damaged );
    SW_EXPECT_TRUE( listEvent[3]._type == sw::HealthEventType::Died );

    listEvent.clear();
    vitals.pollHealthEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

/**
 * @brief [AdventureVitalsTest] 쓰고 읽으면 하트 · 조각 · 두 배 마법 · 체력 · 게이지와 탈진 잠금까지 이어지고, 체력 최대는 하트 수로 다시 정한다
 */
SW_TEST_CASE( AdventureVitalsTest, StateRoundTripKeepsEverything )
{
    sw::AdventureVitals original;
    SW_REQUIRE_TRUE( original.addHeartContainer() );
    SW_EXPECT_FALSE( original.addHeartPiece() );
    SW_EXPECT_FALSE( original.applyDamage( 5 ) );
    SW_EXPECT_TRUE( original.upgradeMagic() );
    SW_EXPECT_TRUE( original.trySpendMagic( 10.0f ) );
    SW_EXPECT_TRUE( original.addStaminaVessel() );
    SW_REQUIRE_TRUE( original.updateStamina( sw::AdventureStaminaAction::Sprint, 1000.0f ) == sw::AdventureStaminaResult::Stop );

    sw::Archive written;
    original.writeState( written );
    sw::AdventureVitals restored;
    sw::Archive         reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_EQUAL( 4, restored.getHeartCount() );
    SW_EXPECT_EQUAL( 1, restored.getHeartPieceCount() );
    SW_EXPECT_EQUAL( 11, restored.computeHealthQuarters() );
    SW_EXPECT_EQUAL( 16, restored.getMaxHealthQuarters() );
    SW_EXPECT_NEAR( 16.0f, restored.getHealth().getSettings()._maxHealth, kVitalsTolerance );
    SW_EXPECT_TRUE( restored.isMagicUpgraded() );
    SW_EXPECT_NEAR( 86.0f, restored.getMagic().getValue(), kVitalsTolerance );
    SW_EXPECT_NEAR( 96.0f, restored.getMagic().getMax(), kVitalsTolerance );
    SW_EXPECT_TRUE( restored.getStamina().isExhausted() );
    SW_EXPECT_NEAR( 120.0f, restored.getStamina().getMax(), kVitalsTolerance );
    SW_EXPECT_EQUAL( 5, restored.heal( 100 ) );
}

/**
 * @brief [AdventureVitalsTest] 결함: 최대를 넘는 체력 · 마력 · 지구력, 상한을 넘거나 음수인 보너스, 받지 않은 두 배 마법의 보너스를 받아들였다. 하트 수 · 조각 범위와 함께 거절하고 상태는 그대로다
 */
SW_TEST_CASE( AdventureVitalsTest, ReadRejectsValuesAboveTheirMax )
{
    sw::AdventureVitals source;
    sw::Archive         written;
    source.writeState( written );
    const size_t size = static_cast<size_t>( written.getSize() );
    // 앞에서: 하트 수(4) 조각(4) 두 배 마법(1), 다음이 체력(첫 칸이 체력 값). 뒤에서: 게이지 하나는 값(4) 보너스(4) 재생 배율(4) 경과(4) 벌칙(4) 잠김(1) = 21 바이트로 마력, 지구력 순이다.
    SW_REQUIRE_TRUE( size > 9 + 42 );
    const size_t magicAt   = size - 42;
    const size_t staminaAt = size - 21;

    sw::AdventureVitals target;
    SW_EXPECT_FALSE( target.applyDamage( 1 ) );
    const auto expectRejected = [&]( size_t offset, auto value )
    {
        SW_EXPECT_FALSE( AdventureVitalsTestUtil::readPatched( written, offset, value, target ) );
        SW_EXPECT_EQUAL( 11, target.computeHealthQuarters() );
        SW_EXPECT_EQUAL( 3, target.getHeartCount() );
    };
    const float32 huge = 1.0e6f;
    expectRejected( 9, huge );              // 체력 > 하트 × 4
    expectRejected( magicAt, huge );        // 마력 > 최대
    expectRejected( staminaAt, huge );      // 지구력 > 최대
    expectRejected( magicAt + 4, 5.0f );    // 받지 않은 두 배 마법의 보너스
    expectRejected( magicAt + 4, -1.0f );   // 음수 보너스
    expectRejected( staminaAt + 4, huge );  // 그릇 상한 초과
    expectRejected( staminaAt + 4, -1.0f ); // 음수 보너스
    expectRejected( staminaAt + 4, std::numeric_limits<float32>::quiet_NaN() );
    expectRejected( 0, static_cast<int32>( 0 ) );  // 하트 수 0
    expectRejected( 0, static_cast<int32>( 21 ) ); // 최대 초과
    expectRejected( 4, static_cast<int32>( 4 ) );  // 조각 = 그릇 하나 분량
    expectRejected( 4, static_cast<int32>( -1 ) );
    expectRejected( 8, static_cast<uint8>( 2 ) );

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
    SW_EXPECT_EQUAL( 11, target.computeHealthQuarters() );
}
