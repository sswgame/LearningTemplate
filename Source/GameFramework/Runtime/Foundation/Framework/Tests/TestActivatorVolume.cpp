#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/AutosaveTriggerComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/MemoryLocalStore.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/TickedPathMoverComponent.h"

// ActivatorVolumeTest — 볼륨 바탕(한 번 · 거리로 들어오기, 안에 있는 동안 되풀이하지 않기)을 자동 저장 볼륨으로 보고, 틱 함수 바닥의 자리 계산을 본다. 슬롯은 메모리 저장소다(nogpu, 물리 없음).

using namespace sw;

namespace
{
    /** @brief 자동 저장 관리자를 게임 범위에 걸고 볼륨과 걷는 오브젝트를 둔 씬입니다. */
    struct AutosaveVolumeRig
    {
        MemoryLocalDatabase        _database;
        unique_ptr<GameLocalStore> _pStore;
        AutosaveManager            _autosave;
        SubsystemScope             _scope;
        GameObjectManager          _manager;
        GameObject*                _pWalker{ nullptr };
        SceneComponent*            _pWalkerRoot{ nullptr };
        AutosaveTriggerComponent*  _pVolume{ nullptr };

        AutosaveVolumeRig()
            : _pStore{ make_unique<GameLocalStore>( make_unique<MemoryLocalStore>( &_database, LocalSealContext{} ) ) }
        {
            AutosaveSettings settings;
            settings._interval = 0.0f;
            SW_REQUIRE_TRUE( _autosave.initialize( settings, *_pStore, AutosaveManager::SaveDelegate::create( []( vector<uint8>& outBytes )
            {
                outBytes.push_back( 1 );
                return true;
            } ),
                                                   AutosaveManager::LoadDelegate::create( []( const vector<uint8>& bytes )
            {
                (void)bytes;
                return true;
            } ) ) );
            _pStore->update();
            SW_REQUIRE_TRUE( _scope.bind( &_autosave ) );

            GameObject* pVolumeObject = _manager.createGameObject( hashed_string( "Volume" ) );
            (void)pVolumeObject->addComponent<SceneComponent>();
            _pVolume = pVolumeObject->addComponent<AutosaveTriggerComponent>();
            _pVolume->configure( AutosaveTrigger::Checkpoint, hashed_string( "gate" ), TagID{} );
            _pWalker     = _manager.createGameObject( hashed_string( "Walker" ) );
            _pWalkerRoot = _pWalker->addComponent<SceneComponent>();
            _pWalkerRoot->setWorldPosition( float3{ 100.0f, 0.0f, 0.0f } );
            _manager.beginPlay();
        }

        void moveWalkerAndTick( float32 x )
        {
            _pWalkerRoot->setWorldPosition( float3{ x, 0.0f, 0.0f } );
            _manager.update( 1.0f / 60.0f );
        }
    };

    struct TickedMoverTestUtil
    {
        static constexpr float32 kTolerance = 1.0e-4f;

        static bool isNear( float32 expected, float32 actual ) { return expected - actual < kTolerance && actual - expected < kTolerance; }
    };
} // namespace

/** @brief [ActivatorVolumeTest] 한 번만 하는 볼륨은 두 번째 활성자를 거절하고, 관리자에 체크포인트 이름을 넘긴다 */
SW_TEST_CASE( ActivatorVolumeTest, OnceFiresOnlyTheFirstTime )
{
    AutosaveVolumeRig     rig;
    game::ScopedGameScope scopedScope( rig._scope );
    SW_EXPECT_FALSE( rig._pVolume->activate( nullptr ) );
    SW_EXPECT_TRUE( rig._pVolume->activate( rig._pWalker ) );
    SW_EXPECT_FALSE( rig._pVolume->activate( rig._pWalker ) );
    SW_EXPECT_TRUE( rig._pVolume->hasFired() );
    SW_EXPECT_TRUE( rig._autosave.getLastCheckpointID() == hashed_string( "gate" ) );
}

/** @brief [ActivatorVolumeTest] 일을 못 하면(관리자가 없는 게임) 발동한 것으로 치지 않는다 */
SW_TEST_CASE( ActivatorVolumeTest, RefusedWorkDoesNotCountAsFired )
{
    AutosaveVolumeRig rig;
    SubsystemScope    emptyScope;
    {
        game::ScopedGameScope scopedScope( emptyScope );
        SW_EXPECT_FALSE( rig._pVolume->activate( rig._pWalker ) );
        SW_EXPECT_FALSE( rig._pVolume->hasFired() );
    }
    game::ScopedGameScope scopedScope( rig._scope );
    SW_EXPECT_TRUE( rig._pVolume->activate( rig._pWalker ) );
}

/** @brief [ActivatorVolumeTest] 물리 몸이 없는 활성자는 거리로 들어오고, 반지름이 0 이면 재지 않는다 */
SW_TEST_CASE( ActivatorVolumeTest, ProximityFiresWhenTheActivatorEnters )
{
    AutosaveVolumeRig     rig;
    game::ScopedGameScope scopedScope( rig._scope );
    rig.moveWalkerAndTick( 1.0f ); // 반지름을 정하기 전이라 재지 않는다
    SW_EXPECT_FALSE( rig._pVolume->hasFired() );

    rig._pVolume->setProximity( rig._pWalker->getHandle(), 2.0f );
    rig.moveWalkerAndTick( 50.0f );
    SW_EXPECT_FALSE( rig._pVolume->hasFired() );
    rig.moveWalkerAndTick( 1.5f );
    SW_EXPECT_TRUE( rig._pVolume->hasFired() );
    SW_EXPECT_TRUE( rig._autosave.getLastCheckpointID() == hashed_string( "gate" ) );
}

/** @brief [TickedPathMoverTest] 왕복은 끝에서 되돌아오고 같은 시각이면 같은 자리다(틱의 순수 함수) */
SW_TEST_CASE( TickedPathMoverTest, PingPongGoesBackFromTheEnd )
{
    const vector<float3> listWaypoint{
        float3{ 6.0f, 0.0f, 0.0f }
    };
    using Util = TickedMoverTestUtil;
    SW_EXPECT_TRUE( Util::isNear( 0.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 0.0 )._x ) );
    SW_EXPECT_TRUE( Util::isNear( 3.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 1.0 )._x ) );
    SW_EXPECT_TRUE( Util::isNear( 3.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 3.0 )._x ) );
    SW_EXPECT_TRUE( Util::isNear( 3.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 5.0 )._x ) ); // 한 바퀴(4 초) 뒤
}

/** @brief [TickedPathMoverTest] 고리는 끝에서 시작으로 한 구간 더 가고, 경유점이 없으면 움직이지 않는다 */
SW_TEST_CASE( TickedPathMoverTest, LoopClosesAndEmptyPathStays )
{
    const vector<float3> listWaypoint{
        float3{2.0f, 0.0f, 0.0f},
        float3{2.0f, 0.0f, 2.0f}
    };
    using Util = TickedMoverTestUtil;
    SW_EXPECT_TRUE( Util::isNear( 2.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 1.0 )._x ) );
    SW_EXPECT_TRUE( Util::isNear( 2.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 2.0 )._z ) );
    SW_EXPECT_TRUE( Util::isNear( 1.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 2.5 )._z ) );
    SW_EXPECT_TRUE( Util::isNear( 0.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 3.0 )._x ) );
    SW_EXPECT_TRUE( Util::isNear( 0.0f, TickedPathMoverComponent::computeOffset( vector<float3>{}, 1.0f, false, 1.0 )._x ) );
}
