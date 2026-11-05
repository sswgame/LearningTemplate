#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace
{
    /** @brief 옮겨 둔 부모와 그 아래 자식입니다. */
    struct ParentDestroyRig
    {
        GameObjectManager _manager;
        GameObject*       _pParent{ nullptr };
        GameObject*       _pChild{ nullptr };
        SceneComponent*   _pChildScene{ nullptr };

        ParentDestroyRig()
        {
            _pParent = _manager.createGameObject( hashed_string( "Parent" ) );
            _pParent->addComponent<SceneComponent>()->setLocalPosition( float3{ 5.0f, 0.0f, 0.0f } );
            _pChild      = _manager.createGameObject( hashed_string( "Child" ) );
            _pChildScene = _pChild->addComponent<SceneComponent>();
            _pChildScene->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );
            SW_REQUIRE_TRUE( _pChild->attachToParent( _pParent ) );
            _manager.beginPlay();
        }

        void flush()
        {
            _manager.update( 1.0f / 60.0f );
            _manager.update( 1.0f / 60.0f );
        }
    };
} // namespace

SW_TEST_CASE( ParentDestroyTest, ChildrenGoWithTheirParentByDefault )
{
    ParentDestroyRig       rig;
    const GameObjectHandle child = rig._pChild->getHandle();
    rig._manager.destroyObject( rig._pParent );
    rig.flush();
    SW_EXPECT_TRUE( rig._manager.resolveGameObject( child ) == nullptr );
}

// 표시한 자식은 루트가 되어 있던 자리에 남는다.
SW_TEST_CASE( ParentDestroyTest, KeptChildStaysWhereItWas )
{
    ParentDestroyRig rig;
    rig._pChild->setKeptWhenParentDestroyed( true );
    const GameObjectHandle child = rig._pChild->getHandle();
    rig._manager.destroyObject( rig._pParent );
    rig.flush();
    SW_REQUIRE_TRUE( rig._manager.resolveGameObject( child ) == rig._pChild );
    SW_EXPECT_TRUE( rig._pChildScene->getParent() == nullptr );
    const float3 world = rig._pChildScene->getWorldPosition();
    SW_EXPECT_NEAR( 6.0f, world._x, 1e-3f );
    SW_EXPECT_NEAR( 2.0f, world._y, 1e-3f );
    SW_EXPECT_NEAR( 3.0f, world._z, 1e-3f );
}

// 자식만 지워도 부모는 그대로다.
SW_TEST_CASE( ParentDestroyTest, DestroyingAChildLeavesTheParent )
{
    ParentDestroyRig       rig;
    const GameObjectHandle parent = rig._pParent->getHandle();
    rig._manager.destroyObject( rig._pChild );
    rig.flush();
    SW_EXPECT_TRUE( rig._manager.resolveGameObject( parent ) == rig._pParent );
    SW_EXPECT_TRUE( rig._pParent->getPrimarySceneComponent()->getChildren().empty() );
}
