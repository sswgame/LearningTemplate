// WorldSeedTest — 서버 월드의 씨앗은 클라이언트가 아는 값이 아니고, 혼자 하는 판 · 클라이언트 · 결정적 시뮬레이션의 씨앗은 되풀이된다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Random/WorldSeed.h"
#include "GameFramework/Runtime/Foundation/Service/Tests/SceneNetLinkTestBinding.h"

using namespace sw;

/** @brief [WorldSeedTest] 넷 링크가 서버인 월드만 부를 때마다 다른 씨앗을 얻는다 — 정해진 씨앗이면 클라이언트가 서버의 수열을 미리 돌린다 */
SW_TEST_CASE( WorldSeedTest, OnlyTheServerWorldHidesItsSeed )
{
    constexpr uint32      kFixedSeed = 7u;
    GameObjectManager     manager;
    const SceneComponent& component = *manager.createGameObject( hashed_string( "Seed" ) )->addComponent<SceneComponent>();
    SW_EXPECT_EQUAL( kFixedSeed, WorldSeed::makeForWorld( component, kFixedSeed ).getValue() ); // 혼자 하는 판
    {
        SceneNetLinkTestBinding client{ manager, NetLinkRole::Client };
        SW_EXPECT_EQUAL( kFixedSeed, WorldSeed::makeForWorld( component, kFixedSeed ).getValue() );
    }
    {
        SceneNetLinkTestBinding server{ manager, NetLinkRole::Server };
        const uint32            firstSeed  = WorldSeed::makeForWorld( component, kFixedSeed ).getValue();
        const uint32            secondSeed = WorldSeed::makeForWorld( component, kFixedSeed ).getValue();
        const uint32            thirdSeed  = WorldSeed::makeForWorld( component, kFixedSeed ).getValue();
        SW_EXPECT_TRUE( firstSeed != secondSeed || secondSeed != thirdSeed );
        SW_EXPECT_TRUE( firstSeed != kFixedSeed || secondSeed != kFixedSeed );
    }
    SW_EXPECT_EQUAL( kFixedSeed, WorldSeed::makeFixed( kFixedSeed ).getValue() );
}
