// VFXEffectPoolTest — 한 번 터지는 효과의 풀: 효과마다 상한까지 오브젝트를 만들고, 모두 재생 중이면 가장 오래 전에 튼 것을 다시 튼다(nogpu).
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/VFX/VFXEffectPool.h"

using namespace sw;

/**
 * @brief [VFXEffectPoolTest] 같은 효과를 잇달아 틀면 상한까지만 오브젝트가 늘고, 그 뒤로는 돌려 쓴다 — 효과마다 풀이 따로다. 전용 서버는 틀지 않는다
 */
SW_TEST_CASE( VFXEffectPoolTest, PlaysUpToTheLimitThenReusesTheOldest )
{
    GameObjectManager   manager;
    VFXEffectPool&      pool = manager.getSubsystem<VFXEffectPool>();
    const hashed_string splashKey( "engine/vfx/footstep_splash.vfx.xml" );
    const hashed_string otherKey( "engine/vfx/footstep_dust.vfx.xml" );
    constexpr uint32    kLimit = 2;
    if constexpr ( build::kWithClientCode == false )
    {
        SW_EXPECT_FALSE( pool.play( splashKey, float3{ 0.0f, 0.0f, 0.0f }, kLimit ) );
        SW_EXPECT_EQUAL( 0u, pool.getPoolSize( splashKey ) );
        return;
    }
    SW_EXPECT_FALSE( pool.play( splashKey, float3{ 0.0f, 0.0f, 0.0f }, 0 ) ); // 상한 0 은 틀 자리가 없다
    for ( uint32 index = 0; index < 4; ++index )
        SW_EXPECT_TRUE( pool.play( splashKey, float3{ static_cast<float32>( index ), 0.0f, 0.0f }, kLimit ) );
    SW_EXPECT_EQUAL( 4u, pool.getPlayCount( splashKey ) );
    SW_EXPECT_EQUAL( kLimit, pool.getPoolSize( splashKey ) );
    SW_EXPECT_TRUE( pool.play( otherKey, float3{ 0.0f, 0.0f, 0.0f }, kLimit ) );
    SW_EXPECT_EQUAL( 1u, pool.getPoolSize( otherKey ) );
    SW_EXPECT_EQUAL( 4u, pool.getPlayCount( splashKey ) );
    pool.onSceneCleared();
    SW_EXPECT_EQUAL( 0u, pool.getPlayCount( splashKey ) );
}
