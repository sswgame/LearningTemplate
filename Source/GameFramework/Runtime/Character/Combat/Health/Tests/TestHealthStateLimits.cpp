#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Character/Combat/Health/ResourceGauge.h"

// 결함: 상태 바이트의 값이 최대를 넘어도 받아 체력 · 자원이 그릇보다 많은 채로 복원됐다 - 키트마다 손으로 막던 것을 읽기 입구에서 막는다.
SW_TEST_CASE( HealthStateLimitsTest, VitalityRefusesHealthAboveItsMaximum )
{
    sw::HealthSettings settings;
    settings._maxHealth = 100.0f;
    settings._maxShield = 10.0f;
    sw::HealthStateMachine written;
    SW_REQUIRE_TRUE( written.initialize( settings ) );
    sw::Archive archive;
    written.writeState( archive );

    sw::HealthStateMachine same;
    SW_REQUIRE_TRUE( same.initialize( settings ) );
    sw::Archive reading( archive.getData(), archive.getSize() );
    SW_EXPECT_TRUE( same.readState( reading ) );
    SW_EXPECT_EQUAL( same.getHealth(), 100.0f );

    sw::HealthSettings smaller = settings;
    smaller._maxHealth         = 50.0f;
    sw::HealthStateMachine lowered;
    SW_REQUIRE_TRUE( lowered.initialize( smaller ) );
    sw::Archive readingLowered( archive.getData(), archive.getSize() );
    SW_EXPECT_FALSE( lowered.readState( readingLowered ) );
    SW_EXPECT_EQUAL( lowered.getHealth(), 50.0f ); // 거절하면 그대로다

    sw::HealthSettings noShield = settings;
    noShield._maxShield         = 5.0f;
    sw::HealthStateMachine shieldLowered;
    SW_REQUIRE_TRUE( shieldLowered.initialize( noShield ) );
    sw::Archive readingShield( archive.getData(), archive.getSize() );
    SW_EXPECT_FALSE( shieldLowered.readState( readingShield ) );
}

SW_TEST_CASE( HealthStateLimitsTest, GaugeRefusesValueAboveItsMaximum )
{
    sw::ResourceGaugeSettings settings;
    settings._max = 100.0f;
    sw::ResourceGauge written;
    SW_REQUIRE_TRUE( written.initialize( settings ) );
    sw::Archive archive;
    written.writeState( archive );

    sw::ResourceGauge same;
    SW_REQUIRE_TRUE( same.initialize( settings ) );
    sw::Archive reading( archive.getData(), archive.getSize() );
    SW_EXPECT_TRUE( same.readState( reading ) );
    SW_EXPECT_EQUAL( same.getValue(), written.getValue() );

    sw::ResourceGaugeSettings smaller = settings;
    smaller._max                      = written.getValue() / 2.0f;
    sw::ResourceGauge lowered;
    SW_REQUIRE_TRUE( lowered.initialize( smaller ) );
    const float32 before = lowered.getValue();
    sw::Archive   readingLowered( archive.getData(), archive.getSize() );
    SW_EXPECT_FALSE( lowered.readState( readingLowered ) );
    SW_EXPECT_EQUAL( lowered.getValue(), before );
}
