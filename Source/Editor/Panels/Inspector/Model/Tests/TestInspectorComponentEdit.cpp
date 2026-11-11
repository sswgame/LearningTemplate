#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

// InspectorComponentEditTest — 기본 컴포넌트 인스펙터 확장의 edit model(값 모으기 · 넣기). 씬 밖의 지역 매니저로 본다.

/**
 * @brief [InspectorComponentEditTest] 카메라 구역은 도로 모으고 바뀐 칸만 넣는다 — 고치지 않은 시야각은 도 ↔ 라디안 왕복으로 바뀌지 않는다
 */
SW_TEST_CASE( InspectorComponentEditTest, CameraAppliesOnlyChangedProperties )
{
    using Edit = sw::editor::InspectorComponentEdit;

    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Camera" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::CameraComponent* pCamera = pObject->addComponent<sw::CameraComponent>();
    SW_REQUIRE_NOT_NULL( pCamera );
    pCamera->setFieldOfViewY( 1.0f );
    pCamera->setOrthographic( false );

    sw::editor::CameraSectionValues values;
    Edit::gatherCamera( *pCamera, values );
    SW_EXPECT_NEAR( values._fovYDegrees, sw::MathUtil::toDegree( 1.0f ), 0.001f );
    SW_EXPECT_EQUAL( values._bOrthographic, static_cast<uint8>( SW_FALSE ) );

    values._bOrthographic = SW_TRUE;
    values._orthoHeight   = 12.0f;
    Edit::applyCamera( *pCamera, values );
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR( pCamera->getOrthoHeight(), 12.0f, 0.0001f );
    SW_EXPECT_EQUAL( pCamera->getFieldOfViewY(), 1.0f ); // 고치지 않은 칸은 그대로다
}

/**
 * @brief [InspectorComponentEditTest] 트랜스폼 구역은 회전을 도로 보이고, 도로 받은 값을 라디안으로 넣는다
 */
SW_TEST_CASE( InspectorComponentEditTest, TransformRotationRoundTripsInDegrees )
{
    using Edit = sw::editor::InspectorComponentEdit;

    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Mover" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::SceneComponent* pScene = pObject->addComponent<sw::SceneComponent>();
    SW_REQUIRE_NOT_NULL( pScene );

    Edit::setLocalRotationDegrees( *pScene, sw::float3{ 0.0f, 90.0f, 0.0f } );
    SW_EXPECT_NEAR( pScene->getLocalRotation()._y, sw::MathUtil::toRadian( 90.0f ), 0.0001f );

    Edit::setLocalPosition( *pScene, sw::float3{ 1.0f, 2.0f, 3.0f } );
    sw::editor::TransformSectionValues values;
    Edit::gatherTransform( *pScene, values );
    SW_EXPECT_NEAR( values._localRotationDegrees._y, 90.0f, 0.001f );
    SW_EXPECT_NEAR( values._localPosition._z, 3.0f, 0.0001f );
}
