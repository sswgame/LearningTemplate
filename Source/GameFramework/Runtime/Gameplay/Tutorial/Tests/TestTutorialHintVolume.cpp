#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/IPlayerNotificationSink.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Tutorial/TutorialHintComponent.h"

// TutorialHintVolumeTest — 볼륨 바탕(`ActivatorVolumeComponent`)의 거리 판정이 트리거 계약(DesignBacklog "트리거 Enter · Stay · Exit 계약")의 경계를 따르는지
// 들어올 때마다 하는 튜토리얼 힌트 볼륨으로 센다: 안에서 생겨남 = 진입, 꺼진 볼륨 · 꺼진 활성자 = 이탈, 한 활성자는 한 길로만 들어온다(nogpu, 물리 없음).

using namespace sw;

namespace
{
    /** @brief 힌트를 센다(알림 화면 자리). */
    class CountingNotificationSink final : public IPlayerNotificationSink
    {
    public:
        uint32 _postCount{ 0 };

        void postNotification( const string& text, float32 durationSeconds, PlayerNotificationKind kind ) override
        {
            (void)text;
            (void)durationSeconds;
            (void)kind;
            ++_postCount;
        }
    };

    /** @brief 들어올 때마다 힌트를 띄우는 볼륨(원점, 반지름 2)과 거리로 재는 활성자 하나를 둔 씬입니다. 알림은 넘긴 창구로 간다(게임 범위의 서비스가 없다). */
    struct HintVolumeRig
    {
        CountingNotificationSink _sink;
        SubsystemScope           _scope; ///< 빈 게임 범위(사용자 설정 서비스가 없으면 힌트는 켜진 것으로 본다)
        GameObjectManager        _manager;
        GameObject*              _pVolumeObject{ nullptr };
        GameObject*              _pWalker{ nullptr };
        TutorialHintComponent*   _pVolume{ nullptr };

        /** @brief @p walkerX 는 활성자가 처음 놓이는 자리입니다. */
        explicit HintVolumeRig( float32 walkerX )
        {
            _pVolumeObject = _manager.createGameObject( hashed_string( "HintVolume" ) );
            (void)_pVolumeObject->addComponent<SceneComponent>();
            _pVolume = _pVolumeObject->addComponent<TutorialHintComponent>();
            _pVolume->configure( "hint", TagID{} );
            _pVolume->setOnce( false );
            _pVolume->setNotificationSink( &_sink );
            _pWalker                    = _manager.createGameObject( hashed_string( "Walker" ) );
            SceneComponent* pWalkerRoot = _pWalker->addComponent<SceneComponent>();
            pWalkerRoot->setWorldPosition( float3{ walkerX, 0.0f, 0.0f } );
            _pVolume->setProximity( _pWalker->getHandle(), 2.0f );
            _manager.beginPlay();
        }

        void tick() { _manager.update( 1.0f / 60.0f ); }
    };
} // namespace

/** @brief [TutorialHintVolumeTest] 거리 판정: 안에서 생겨난 활성자는 첫 틱에 들어오고, 안에 있는 동안은 다시 하지 않는다 */
SW_TEST_CASE( TutorialHintVolumeTest, ProximitySpawnInsideCountsAsEnter )
{
    HintVolumeRig         rig( 1.0f );
    game::ScopedGameScope scopedScope( rig._scope );
    rig.tick();
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
    rig.tick();
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
}

/** @brief [TutorialHintVolumeTest] 꺼진 볼륨은 안의 활성자를 내보낸다 — 다시 켜면 안에 있는 활성자가 다시 들어온다(물리 트리거가 몸을 놓고 다시 얹는 것과 같다) */
SW_TEST_CASE( TutorialHintVolumeTest, DisabledVolumeExitsItsActivator )
{
    HintVolumeRig         rig( 1.0f );
    game::ScopedGameScope scopedScope( rig._scope );
    rig.tick();
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
    rig._pVolumeObject->setActive( false );
    rig.tick();
    rig._pVolumeObject->setActive( true );
    rig.tick();
    SW_EXPECT_EQUAL( 2u, rig._sink._postCount );

    rig._pVolume->setActive( false ); // 컴포넌트만 끄고 켜도 같다
    rig.tick();
    rig._pVolume->setActive( true );
    rig.tick();
    SW_EXPECT_EQUAL( 3u, rig._sink._postCount );
}

/** @brief [TutorialHintVolumeTest] 꺼진 활성자는 나간 것이다 — 다시 켜지면 안에서 다시 들어온다(비활성 · 로그아웃의 끝, 로그인의 시작) */
SW_TEST_CASE( TutorialHintVolumeTest, DisabledActivatorExits )
{
    HintVolumeRig         rig( 1.0f );
    game::ScopedGameScope scopedScope( rig._scope );
    rig.tick();
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
    rig._pWalker->setActive( false );
    rig.tick();
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
    rig._pWalker->setActive( true );
    rig.tick();
    SW_EXPECT_EQUAL( 2u, rig._sink._postCount );
}

/** @brief [TutorialHintVolumeTest] 거리로 재는 활성자의 몸 겹침은 받지 않는다 — 한 진입이 두 길로 두 번 되지 않는다. 다른 몸의 겹침은 받고, 상대의 감지 범위(트리거)는 받지 않는다 */
SW_TEST_CASE( TutorialHintVolumeTest, ProximityActivatorIgnoresItsOverlap )
{
    HintVolumeRig         rig( 50.0f );
    game::ScopedGameScope scopedScope( rig._scope );
    OverlapInfo           walkerOverlap;
    walkerOverlap._pOther = rig._pWalker;
    rig._pVolume->onOverlapBegin( walkerOverlap );
    SW_EXPECT_EQUAL( 0u, rig._sink._postCount );

    GameObject* pRunner = rig._manager.createGameObject( hashed_string( "Runner" ) );
    OverlapInfo runnerOverlap;
    runnerOverlap._pOther = pRunner;
    rig._pVolume->onOverlapBegin( runnerOverlap );
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );

    OverlapInfo sensorOverlap;
    sensorOverlap._pOther        = pRunner;
    sensorOverlap._bOtherTrigger = SW_TRUE;
    rig._pVolume->onOverlapBegin( sensorOverlap );
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
}
