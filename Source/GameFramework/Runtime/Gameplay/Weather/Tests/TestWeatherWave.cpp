#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Weather/WeatherWaveComponent.h"

// WeatherWaveTest — 날씨 값 → 파도 진폭 배율(잔잔 1 · 폭풍 stormScale), 날씨가 없는 씬은 배율 1. 폭풍 속 배의 롤 · 피치는 시나리오 game.empty.boat 가 본다.

using namespace sw;

/** @brief [WeatherWaveTest] 날씨 값 0 이면 1, 1 이면 폭풍 배율, 그 사이는 직선이고 음수는 0 으로 본다 */
SW_TEST_CASE( WeatherWaveTest, WeatherValueMapsToAmplitudeScale )
{
    SW_EXPECT_NEAR( 1.0f, WeatherWaveComponent::computeAmplitudeScale( 0.0f, 4.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 4.0f, WeatherWaveComponent::computeAmplitudeScale( 1.0f, 4.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 2.5f, WeatherWaveComponent::computeAmplitudeScale( 0.5f, 4.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, WeatherWaveComponent::computeAmplitudeScale( -2.0f, 4.0f ), 1.0e-6f );
}

/** @brief [WeatherWaveTest] 판 상태가 없는 씬에서는 틱이 물의 배율을 1 로 둔다(앞서 바꿔 둔 값을 되돌린다) */
SW_TEST_CASE( WeatherWaveTest, NoWeatherKeepsTheWrittenWaves )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Sea" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    WaterBodyComponent*   pWater = pObject->addComponent<WaterBodyComponent>();
    WeatherWaveComponent* pWaves = pObject->addComponent<WeatherWaveComponent>();
    SW_REQUIRE_NOT_NULL( pWater );
    SW_REQUIRE_NOT_NULL( pWaves );
    pWater->setMaterialPath( "" );
    pWater->setWaveAmplitudeScale( 3.0f );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_NEAR( 1.0f, pWater->getWaveAmplitudeScale(), 1.0e-6f );
}
