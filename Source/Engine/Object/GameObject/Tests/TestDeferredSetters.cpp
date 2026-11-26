#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/TickModeTestComponents.h"

// DeferredSettersTest — 틱 중(구조가 얼었을 때) 미뤄지는 세터가 지금 값과 견주기 **전에** 미루는지 본다. 견주기가 먼저면 앞서 미룬 반대 요청
// (끄기 → 다시 켜기, A → B → A)이 큐에 있는데 지금 값과 같다고 버려, 틱 뒤에는 마지막 요청이 아니라 앞 요청이 남는다(nogpu).

using namespace sw;

namespace
{
    constexpr float32 kFrameSeconds = 1.0f / 60.0f;

    /** @brief 부모 오브젝트와, 그 아래 붙은 자식 오브젝트(첫 틱에 테스트의 일을 하는 컴포넌트를 단다)입니다. */
    struct DeferredSetterRig
    {
        GameObjectManager                 _manager;
        GameObject*                       _pParent{ nullptr };
        GameObject*                       _pChild{ nullptr };
        SceneComponent*                   _pParentScene{ nullptr };
        SceneComponent*                   _pChildScene{ nullptr };
        DeferredSetterProbeTestComponent* _pProbe{ nullptr };

        DeferredSetterRig()
        {
            _pParent      = _manager.createGameObject( hashed_string( "Parent" ) );
            _pParentScene = _pParent->addComponent<SceneComponent>();
            _pChild       = _manager.createGameObject( hashed_string( "A" ) );
            _pChildScene  = _pChild->addComponent<SceneComponent>();
            _pProbe       = _pChild->addComponent<DeferredSetterProbeTestComponent>();
            SW_REQUIRE_TRUE( _pChild->attachToParent( _pParent ) );
            _manager.beginPlay();
        }

        /** @brief 다음 프레임의 첫 틱에 @p func 를 부르고 프레임을 끝까지 돌립니다(틱 뒤 미룬 일까지 적용된다). */
        void runInTick( Delegate<void( Component& )> func )
        {
            _pProbe->_onFirstTick = std::move( func );
            _manager.update( kFrameSeconds );
            SW_REQUIRE_TRUE( _pProbe->_bRanFrozen ); // 정말 틱 중(구조가 얼었을 때) 불렀다
        }
    };
} // namespace

/**
 * @brief [DeferredSettersTest] 틱 중 주 틱 끄기 → 다시 켜기는 틱 뒤 켜진 채로 남는다(마지막 요청)
 */
SW_TEST_CASE( DeferredSettersTest, CanEverTickOffThenOnKeepsLastRequest )
{
    DeferredSetterRig rig;
    rig.runInTick( Delegate<void( Component& )>( []( Component& self )
    {
        self.setCanEverTick( false );
        self.setCanEverTick( true );
    } ) );
    SW_EXPECT_TRUE( rig._pProbe->canEverTick() );
}

/**
 * @brief [DeferredSettersTest] 틱 중 오브젝트 이름 A → B → A 는 틱 뒤 A 로 남고 이름 검색도 A 로 찾는다
 */
SW_TEST_CASE( DeferredSettersTest, ObjectNameBackAndForthKeepsLastRequest )
{
    DeferredSetterRig rig;
    rig.runInTick( Delegate<void( Component& )>( []( Component& self )
    {
        self.getOwner()->setName( hashed_string( "B" ) );
        self.getOwner()->setName( hashed_string( "A" ) );
    } ) );
    SW_EXPECT_TRUE( rig._pChild->getName().isEqual( hashed_string( "A" ), NameCase::CaseSensitive ) );
    SW_EXPECT_TRUE( rig._manager.findGameObjectByName( hashed_string( "A" ) ) == rig._pChild );
}

/**
 * @brief [DeferredSettersTest] 틱 중 오브젝트 떼기 → 같은 부모에 다시 붙이기는 틱 뒤 붙은 채로 남는다
 */
SW_TEST_CASE( DeferredSettersTest, ObjectDetachThenAttachKeepsParent )
{
    DeferredSetterRig rig;
    GameObject* const pParent   = rig._pParent;
    bool              bAccepted = false;
    bool* const       pAccepted = &bAccepted; // 틱 워커에서 단언하지 않고 적어 두었다가 틱 뒤에 본다
    rig.runInTick( Delegate<void( Component& )>( [pParent, pAccepted]( Component& self )
    {
        self.getOwner()->detachFromParent();
        *pAccepted = self.getOwner()->attachToParent( pParent );
    } ) );
    SW_EXPECT_TRUE( bAccepted );
    SW_EXPECT_TRUE( rig._pChild->getParent() == rig._pParent );
}

/**
 * @brief [DeferredSettersTest] 틱 중 씬 컴포넌트 떼기 → 같은 부모에 다시 붙이기는 틱 뒤 붙은 채로 남는다
 */
SW_TEST_CASE( DeferredSettersTest, SceneComponentDetachThenAttachKeepsParent )
{
    DeferredSetterRig     rig;
    SceneComponent* const pChildScene  = rig._pChildScene;
    SceneComponent* const pParentScene = rig._pParentScene;
    bool                  bAccepted    = false;
    bool* const           pAccepted    = &bAccepted;
    rig.runInTick( Delegate<void( Component& )>( [pChildScene, pParentScene, pAccepted]( Component& /*self*/ )
    {
        pChildScene->detachFromComponent();
        *pAccepted = pChildScene->attachToComponent( pParentScene, AttachRule::KeepRelative );
    } ) );
    SW_EXPECT_TRUE( bAccepted );
    SW_EXPECT_TRUE( rig._pChildScene->getParent() == rig._pParentScene );
}
