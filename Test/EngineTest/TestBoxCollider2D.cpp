#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// 2D 박스 콜라이더 — 물리가 보는 상자.

namespace
{
    /** @brief 크기 1 · 오프셋 (1, 0) 의 콜라이더를 단 오브젝트를 만들어 @p pParentRoot 에 붙입니다. */
    sw::BoxCollider2DComponent* spawnCrate( sw::GameObjectManager& manager, sw::GameObject* pParent )
    {
        sw::GameObject* pCrate = manager.createGameObject( sw::hashed_string( "Crate" ) );
        if ( pCrate == nullptr )
            return nullptr;
        sw::BoxCollider2DComponent* pBox = pCrate->addComponent<sw::BoxCollider2DComponent>();
        if ( pBox == nullptr )
            return nullptr;
        pBox->setOffsetScale( sw::float2{ 1.0f, 1.0f } );
        pBox->setOffsetPosition( sw::float2{ 1.0f, 0.0f } );
        if ( pParent != nullptr && pCrate->attachToParent( pParent ) == false )
            return nullptr;
        return pBox;
    }

    /** @brief 상자의 최소 · 최대가 기대와 같은지(오차 1e-4) 봅니다. */
    bool isBox( const sw::BoxCollider2DComponent* pBox, const sw::float2& expectedMin, const sw::float2& expectedMax )
    {
        sw::float2 minB{};
        sw::float2 maxB{};
        pBox->getBounds( minB, maxB );
        return sw::MathUtil::nearEqual( minB._x, expectedMin._x, 1e-4f ) && sw::MathUtil::nearEqual( minB._y, expectedMin._y, 1e-4f ) &&
               sw::MathUtil::nearEqual( maxB._x, expectedMax._x, 1e-4f ) && sw::MathUtil::nearEqual( maxB._y, expectedMax._y, 1e-4f );
    }
} // namespace

/**
 * @brief [BoxCollider2DTest] 콜라이더 상자는 월드 스케일을 따르고, 오프셋도 그 스케일로 늘어난다
 * @details 예전에는 상자 크기(`_offsetScale`)와 오프셋을 월드 위치에 그대로 더해, 부모나 자기를 키운 콜라이더가 그려진 모습보다 작았다 — 큰 적이
 *          작은 상자로 맞았다. 유니티 `BoxCollider2D.size` · 언리얼 박스 범위는 트랜스폼의 스케일을 받는다.
 */
SW_TEST_CASE( BoxCollider2DTest, BoxFollowsWorldScale )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pGiant = manager.createGameObject( sw::hashed_string( "Giant" ) );
    sw::SceneComponent*   pRoot  = pGiant->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( sw::float3{ 10.0f, 0.0f, 0.0f } );
    pRoot->setLocalScale( sw::float3{ 2.0f, 3.0f, 1.0f } );
    sw::BoxCollider2DComponent* pBox = spawnCrate( manager, pGiant );
    SW_ASSERT_NOT_NULL( pBox );
    manager.flushSceneTransforms();

    // 중심 = 부모 (10, 0) + 오프셋 (1, 0) × 스케일 (2, 3) = (12, 0). 크기 1 × (2, 3) = (2, 3).
    SW_EXPECT_TRUE( isBox( pBox, sw::float2{ 11.0f, -1.5f }, sw::float2{ 13.0f, 1.5f } ) );
}

/**
 * @brief [BoxCollider2DTest] 돈 콜라이더는 돈 상자를 덮는 축 정렬 상자다(물리는 축 정렬 상자로 판정한다)
 * @details Z 로 90 도 돌면 (2, 3) 크기 상자가 (3, 2) 를 덮는다. 오프셋도 돈다 — (1, 0) × 스케일이 (0, 2) 가 된다. 언리얼 `FBox::TransformBy` 와 같은
 *          계산이다(축마다 회전 · 스케일 성분의 절댓값으로 반 크기를 모은다).
 */
SW_TEST_CASE( BoxCollider2DTest, RotatedBoxIsCoveredByItsAxisAlignedBounds )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTurned = manager.createGameObject( sw::hashed_string( "Turned" ) );
    sw::SceneComponent*   pRoot   = pTurned->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalScale( sw::float3{ 2.0f, 3.0f, 1.0f } );
    pRoot->setLocalRotation( sw::float3{ 0.0f, 0.0f, sw::MathUtil::HalfPi } );
    sw::BoxCollider2DComponent* pBox = spawnCrate( manager, pTurned );
    SW_ASSERT_NOT_NULL( pBox );
    manager.flushSceneTransforms();

    sw::float2 minB{};
    sw::float2 maxB{};
    pBox->getBounds( minB, maxB );
    SW_EXPECT_NEAR_EQUAL( 3.0f, maxB._x - minB._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, maxB._y - minB._y, 1e-4f );
    // 오프셋 (1, 0) 은 스케일 (2, ·) 로 2 가 되고, 90 도 돌아 Y 축 위 2 에 놓인다(부호는 회전 방향을 따른다).
    SW_EXPECT_NEAR_EQUAL( 0.0f, ( minB._x + maxB._x ) * 0.5f, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, sw::MathUtil::abs( ( minB._y + maxB._y ) * 0.5f ), 1e-4f );
}
