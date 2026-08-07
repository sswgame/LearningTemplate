/**
 * @file Test/CoreSmokeTest/SmokeTests.cpp
 * @brief Core 단일 라이브러리 스모크 테스트 (구 모듈별 스텁 통합)
 */

#include "TestFramework.h"
#include "Core/Utility/Math/Math.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Object/GameObject.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Graphics/RHI/RHITypes.h"

SW_TEST_CASE( CoreSmoke, MathBasics )
{
	SW_EXPECT_TRUE( true );
}

SW_TEST_CASE( CoreSmoke, StringBasics )
{
	SW_EXPECT_TRUE( true );
}

SW_TEST_CASE( CoreSmoke, ObjectBasics )
{
	SW_EXPECT_TRUE( true );
}

SW_TEST_CASE( CoreSmoke, ReflectionBasics )
{
	SW_EXPECT_TRUE( true );
}

SW_TEST_CASE( CoreSmoke, RHITypesBasics )
{
	sw::RHITextureHandle handle = 0;
	SW_EXPECT_EQUAL( handle, static_cast<sw::RHITextureHandle>( 0 ) );
}
