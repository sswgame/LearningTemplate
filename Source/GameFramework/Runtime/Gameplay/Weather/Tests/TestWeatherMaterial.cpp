#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Weather/WeatherMaterialComponent.h"

// WeatherMaterialTest — 날씨 값 → 머티리얼 파라미터 묶음 칸: 적은 채널은 날씨 값(날씨가 없으면 0), 빈 채널은 파일 값 그대로. 비 온 땅 · 링은 시나리오 game.shooter3d.rainyard 가 본다.

using namespace sw;

/** @brief [WeatherMaterialTest] 날씨가 없는 씬은 적은 채널이 0 이고, 적지 않은 채널(설선 높이)은 파일 값을 지킨다 */
SW_TEST_CASE( WeatherMaterialTest, NamedChannelsTakeWeatherAndEmptyChannelsKeepTheFile )
{
    WeatherMaterialBinding binding;
    binding._parameter = hashed_string( "Weather" );
    binding._valueX    = hashed_string( "wetness" );
    binding._valueY    = hashed_string( "puddle" );
    const float4 fileValue{ 0.25f, 0.5f, 0.75f, 18.0f };
    const float4 value = WeatherMaterialComponent::computeValue( binding, fileValue, nullptr );
    SW_EXPECT_NEAR( 0.0f, value._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, value._y, 1.0e-6f );
    SW_EXPECT_NEAR( 0.75f, value._z, 1.0e-6f );
    SW_EXPECT_NEAR( 18.0f, value._w, 1.0e-6f );
}
