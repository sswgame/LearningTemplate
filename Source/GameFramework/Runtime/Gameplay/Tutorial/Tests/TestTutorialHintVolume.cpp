#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/IPlayerNotificationSink.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Tests/SceneNetLinkTestBinding.h"
#include "GameFramework/Runtime/Gameplay/Tutorial/TutorialHintTriggerComponent.h"

// TutorialHintVolumeTest — 볼륨 바탕(`TriggerComponent`)의 거리 판정이 트리거 계약(docs/agents/Gameplay.md "트리거 · 물리")의 경계를 따르는지
// 들어올 때마다 하는 튜토리얼 힌트 볼륨으로 센다: 안에서 생겨남 = 진입, 꺼진 볼륨 · 꺼진 활성자 = 이탈, 한 활성자는 한 길로만 들어온다(nogpu, 물리 없음).

using namespace sw;

namespace
{
    /** @brief 힌트를 센다(알림 화면 자리). */
    class CountingNotificationSink final : public IPlayerNotificationSink
    {
    public:
        string                 _lastText{};
        PlayerNotificationKind _lastKind{};
        uint32                 _postCount{ 0 };

        void postNotification( const string& text, float32 durationSeconds, PlayerNotificationKind kind ) override
        {
            (void)durationSeconds;
            _lastText = text;
            _lastKind = kind;
            ++_postCount;
        }
    };

    /** @brief 들어올 때마다 힌트를 띄우는 볼륨(원점, 반지름 2)과 거리로 재는 활성자 하나를 둔 씬입니다. 알림은 넘긴 창구로 간다(게임 범위의 서비스가 없다). */
    struct HintVolumeRig
    {
        CountingNotificationSink      _sink;
        SubsystemScope                _scope; ///< 게임 범위 — 힌트 설정만 든 사용자 설정 서비스를 둔다(없으면 엔진 범위의 서비스가 보여 그 실행의 스키마에 달린다)
        GameObjectManager             _manager;
        GameObject*                   _pVolumeObject{ nullptr };
        GameObject*                   _pWalker{ nullptr };
        TutorialHintTriggerComponent* _pVolume{ nullptr };

        /** @brief @p walkerX 는 활성자가 처음 놓이는 자리입니다. */
        explicit HintVolumeRig( float32 walkerX )
        {
            const bool bSchemaLoaded = _scope.emplaceSubsystem<UserSettingsManager>().loadSchemaFromXMLText(
                "<UserSettingsSchema owner=\"engine\" version=\"1\"><Category id=\"gameplay\"/><Setting id=\"gameplay.showTutorials\" category=\"gameplay\" type=\"Bool\" default=\"true\"/></UserSettingsSchema>", "test" );
            SW_EXPECT_TRUE( bSchemaLoaded );
            _pVolumeObject = _manager.createGameObject( hashed_string( "HintVolume" ) );
            (void)_pVolumeObject->addComponent<SceneComponent>();
            _pVolume = _pVolumeObject->addComponent<TutorialHintTriggerComponent>();
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
    GameObjectEvents::broadcast( *rig._pVolumeObject, &GameObjectEvents::_overlapBegin, walkerOverlap );
    SW_EXPECT_EQUAL( 0u, rig._sink._postCount );

    GameObject* pRunner = rig._manager.createGameObject( hashed_string( "Runner" ) );
    OverlapInfo runnerOverlap;
    runnerOverlap._pOther = pRunner;
    GameObjectEvents::broadcast( *rig._pVolumeObject, &GameObjectEvents::_overlapBegin, runnerOverlap );
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );

    OverlapInfo sensorOverlap;
    sensorOverlap._pOther        = pRunner;
    sensorOverlap._bOtherTrigger = SW_TRUE;
    GameObjectEvents::broadcast( *rig._pVolumeObject, &GameObjectEvents::_overlapBegin, sensorOverlap );
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
}

/** @brief [TutorialHintVolumeTest] 판정 자리 Local: 클라이언트 넷 월드도 제 진입으로 힌트를 띄운다 — 다만 이 월드의 로컬 플레이어가 조종하지 않는 폰(원격 플레이어의 복제 폰)은 거른다 */
SW_TEST_CASE( TutorialHintVolumeTest, ClientWorldShowsHintsOnlyForItsLocalPlayer )
{
    HintVolumeRig         rig( 50.0f );
    game::ScopedGameScope scopedScope( rig._scope );
    GameObject*           pRemotePlayer = rig._manager.createGameObject( hashed_string( "RemotePlayer" ) );
    (void)pRemotePlayer->addComponent<PawnComponent>();        // 쥔 로컬 조종자가 없다 — 복제된 원격 플레이어 폰 자리
    SW_EXPECT_TRUE( rig._pVolume->activate( pRemotePlayer ) ); // 넷 링크 없는 월드(혼자 하는 게임)는 거르지 않는다
    {
        SceneNetLinkTestBinding client( rig._manager, NetLinkRole::Client );
        SW_EXPECT_TRUE( rig._pVolume->activate( rig._pWalker ) ); // 폰이 아닌 활성자 — 거를 원격 플레이어가 아니다
        SW_EXPECT_FALSE( rig._pVolume->activate( pRemotePlayer ) );
    }
    SW_EXPECT_EQUAL( 2u, rig._sink._postCount );
}

/** @brief [TutorialHintVolumeTest] 한 번 볼륨: 튜토리얼을 끈 동안 · 글이 빈 동안은 띄우지 않고 발동으로 치지도 않는다 — 다시 켜면 그때 힌트 종류로 한 번 띄운다 */
SW_TEST_CASE( TutorialHintVolumeTest, SuppressedHintIsNotSpent )
{
    HintVolumeRig         rig( 50.0f );
    game::ScopedGameScope scopedScope( rig._scope );
    rig._pVolume->setOnce( true );
    UserSettingsManager* const pSettings = rig._scope.find<UserSettingsManager>();
    SW_REQUIRE_NOT_NULL( pSettings );
    const hashed_string showTutorials( "gameplay.showTutorials" );
    SW_REQUIRE_TRUE( pSettings->setPendingValue( showTutorials, "false" ) == UserSettingSetResult::Accepted );
    (void)pSettings->applyPending(); // 대상 없는 설정이라 적용할 곳이 없다 — 확정 값만 바뀐다
    SW_EXPECT_FALSE( rig._pVolume->activate( rig._pWalker ) );

    SW_REQUIRE_TRUE( pSettings->setPendingValue( showTutorials, "true" ) == UserSettingSetResult::Accepted );
    (void)pSettings->applyPending();
    rig._pVolume->configure( "", TagID{} );
    SW_EXPECT_FALSE( rig._pVolume->activate( rig._pWalker ) );
    SW_EXPECT_EQUAL( 0u, rig._sink._postCount );

    rig._pVolume->configure( "[action=Interact] opens the door", TagID{} );
    SW_EXPECT_TRUE( rig._pVolume->activate( rig._pWalker ) );
    SW_EXPECT_FALSE( rig._pVolume->activate( rig._pWalker ) ); // 한 번 했다
    SW_EXPECT_EQUAL( 1u, rig._sink._postCount );
    SW_EXPECT_EQUAL( string( "[action=Interact] opens the door" ), rig._sink._lastText );
    SW_EXPECT_TRUE( rig._sink._lastKind == PlayerNotificationKind::Hint );
}
