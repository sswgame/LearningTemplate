#include "pch.h"

#include "Engine/Physics/PhysicsSettings.h"

#include "TestFramework/TestFramework.h"

// 물리 설정 표 — 레이어 행렬은 대칭으로 풀리고, 모르는 이름 · 겹친 이름은 오류다.

/**
 * @brief [PhysicsSettingsTest] 레이어 표는 대칭 행렬로 풀린다 — 한쪽만 적어도 둘이 부딪히고, 적지 않은 쌍은 부딪히지 않는다
 */
SW_TEST_CASE( PhysicsSettingsTest, LayerTableIsSymmetric )
{
    sw::PhysicsSettings settings;
    SW_ASSERT_TRUE( settings.loadFromXMLText( R"(<PhysicsSettings>
        <_listLayer>
            <PhysicsLayerDef _name="Default"><_listCollidesWith><item>Default</item><item>Debris</item></_listCollidesWith></PhysicsLayerDef>
            <PhysicsLayerDef _name="Debris"><_listCollidesWith /></PhysicsLayerDef>
            <PhysicsLayerDef _name="Ghost"><_listCollidesWith /></PhysicsLayerDef>
        </_listLayer>
        <_listMaterial><PhysicsMaterialDef _name="Ice" _friction="0.02" /></_listMaterial>
    </PhysicsSettings>)" ) );
    uint8 debris = 0;
    SW_ASSERT_TRUE( settings.findLayerIndex( sw::hashed_string( "Debris" ), debris ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( 1 ), debris );
    const sw::CollisionLayers layers = settings.makeCollisionLayers();
    SW_EXPECT_TRUE( layers.shouldCollide( 0, 1 ) );
    SW_EXPECT_TRUE( layers.shouldCollide( 1, 0 ) );
    SW_EXPECT_FALSE( layers.shouldCollide( 1, 1 ) );
    SW_EXPECT_FALSE( layers.shouldCollide( 2, 0 ) );
    SW_ASSERT_NOT_NULL( settings.findMaterial( sw::hashed_string( "Ice" ) ) );
    SW_EXPECT_NEAR_EQUAL( 0.02f, settings.findMaterial( sw::hashed_string( "Ice" ) )->_friction, 1e-6f );
}

/**
 * @brief [PhysicsSettingsTest] 모르는 레이어 이름 · 모르는 키는 읽기 오류다
 */
SW_TEST_CASE( PhysicsSettingsTest, UnknownNamesAreLoadErrors )
{
    SW_TEST_DEFENSIVE_SCOPE( "unknown layer and key names are rejected" );
    sw::PhysicsSettings settings;
    SW_EXPECT_FALSE( settings.loadFromXMLText(
        R"(<PhysicsSettings><_listLayer><PhysicsLayerDef _name="Default"><_listCollidesWith><item>Nope</item></_listCollidesWith></PhysicsLayerDef></_listLayer></PhysicsSettings>)" ) );
    SW_EXPECT_FALSE( settings.loadFromXMLText( R"(<PhysicsSettings _gravityy="0,-9.8,0" />)" ) );
}

/**
 * @brief [PhysicsSettingsTest] 엔진의 설정 표(`engine/physics/physicssettings.xml`)가 읽히고 기본 레이어 · 재질을 담는다
 */
SW_TEST_CASE( PhysicsSettingsTest, EngineSettingsFileLoads )
{
    sw::PhysicsSettings settings;
    SW_ASSERT_TRUE( settings.loadFromResource( "engine/physics/physicssettings.xml" ) );
    uint8 layer = 0;
    SW_EXPECT_TRUE( settings.findLayerIndex( sw::hashed_string( "Ragdoll" ), layer ) );
    SW_EXPECT_TRUE( settings.findLayerIndex( sw::hashed_string( "Debris" ), layer ) );
    SW_EXPECT_NOT_NULL( settings.findMaterial( sw::hashed_string( "Flesh" ) ) );
}
