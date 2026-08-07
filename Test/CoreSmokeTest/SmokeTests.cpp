/**
 * @file Test/CoreSmokeTest/SmokeTests.cpp
 * @brief Lightweight Core link + basic invariant smoke tests
 */

#include "TestFramework.h"
#include "Core/Utility/Math/VectorMath.h"
#include "Core/Utility/String/hashed_string.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Object/GameObject.h"
#include "Core/Game/GameState.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Common/CoreServices.h"

SW_TEST_CASE( CoreSmoke, MathBasics )
{
	sw::float2 v( 3.0f, 4.0f );
	SW_EXPECT_NEAR( 5.0f, v.getLength(), 1e-5f );
}

SW_TEST_CASE( CoreSmoke, StringBasics )
{
	sw::hashed_string a( "Smoke" );
	sw::hashed_string b( "smoke" );
	SW_EXPECT_TRUE( a == b );
	SW_EXPECT_FALSE( sw::StringUtil::isNullOrEmpty( "ok" ) );
}

SW_TEST_CASE( CoreSmoke, ObjectBasics )
{
	sw::GameObject obj( sw::hashed_string( "SmokeObject" ) );
	SW_EXPECT_STREQ( "SmokeObject", obj.getName().c_str() );
	SW_EXPECT_TRUE( obj.getObjectId() != 0 );
	SW_EXPECT_TRUE( obj.isActive() );
}

SW_TEST_CASE( CoreSmoke, ReflectionBasics )
{
	SW_EXPECT_NOT_NULL( &sw::getTypeRegistry() );
}

SW_TEST_CASE( CoreSmoke, GameStateBasics )
{
	sw::setGameState( sw::GameState::Stopped );
	SW_EXPECT_EQUAL( static_cast<int>( sw::GameState::Stopped ), static_cast<int>( sw::getGameState() ) );
}

SW_TEST_CASE( CoreSmoke, RHITypesBasics )
{
	sw::RHITextureHandle handle = 0;
	SW_EXPECT_EQUAL( handle, static_cast<sw::RHITextureHandle>( 0 ) );
}
