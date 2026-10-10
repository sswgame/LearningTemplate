#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"

#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorTransformCommandsTest] 정렬은 월드 위치로 한다 — 돌고 커진 부모 아래의 오브젝트도 맞춘 축에 선다
 * @details 월드 위치를 고쳐 쓴다(`SceneComponent::setWorldPosition`). 월드 축의 차이를 **로컬** 축에 더하면, 부모가 Y 로 90° 돌고 두 배로
 *          커졌을 때 로컬 X 는 월드 -Z 방향이고 두 배로 움직여 맞춘 값과 다른 자리로 간다.
 */
SW_TEST_CASE( EditorTransformCommandsTest, AlignUsesWorldPositionsUnderARotatedScaledParent )
{
    GameObjectManager manager;
    GameObject*       pParent   = manager.createGameObject( hashed_string( "Turntable" ) );
    SceneComponent*   pParentSc = pParent->addComponent<SceneComponent>();
    pParentSc->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    pParentSc->setLocalRotation( float3( 0.0f, MathUtil::kHalfPi, 0.0f ) );
    pParentSc->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );
    GameObject*     pChild   = manager.createGameObject( hashed_string( "OnTurntable" ) );
    SceneComponent* pChildSc = pChild->addComponent<SceneComponent>();
    pChildSc->setLocalPosition( float3( 1.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
    GameObject*     pRoot   = manager.createGameObject( hashed_string( "Marker" ) );
    SceneComponent* pRootSc = pRoot->addComponent<SceneComponent>();
    pRootSc->setLocalPosition( float3( 5.0f, 0.0f, 3.0f ) );
    manager.flushSceneTransforms();

    EditorTransformCommands::alignObjects( { pChild, pRoot }, AlignAxis::X, AlignType::Min );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 5.0f, pChildSc->getWorldPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pRootSc->getWorldPosition()._x, 1e-4f );
}

/**
 * @brief [EditorTransformCommandsTest] 바닥 붙이기는 월드 Y 로 한다 — 부모 아래의 오브젝트도 월드 바닥에 선다
 * @details 월드 위치를 읽고 **로컬** 칸에 쓰면 부모가 위로 옮겨져 있기만 해도 그만큼 뜬다. 크기도 월드로 잰다.
 */
SW_TEST_CASE( EditorTransformCommandsTest, SnapToGroundPutsAParentedObjectOnWorldGround )
{
    GameObjectManager manager;
    GameObject*       pParent   = manager.createGameObject( hashed_string( "Shelf" ) );
    SceneComponent*   pParentSc = pParent->addComponent<SceneComponent>();
    pParentSc->setLocalPosition( float3( 0.0f, 10.0f, 0.0f ) );
    pParentSc->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );
    GameObject*    pCrate = manager.createGameObject( hashed_string( "Crate" ) );
    MeshComponent* pMesh  = pCrate->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_ASSERT_TRUE( pCrate->attachToParent( pParent ) );
    manager.flushSceneTransforms();

    EditorTransformCommands::snapObjectsToGround( { pCrate } );
    manager.flushSceneTransforms();
    // 단위 상자 · 월드 Y 스케일 2 — 바닥까지 1.
    SW_EXPECT_NEAR_EQUAL( 1.0f, pMesh->getWorldPosition()._y, 1e-4f );
}

/**
 * @brief [EditorTransformCommandsTest] 붙여 넣은 값 · 프리셋은 컴포넌트에 알려진다 — 위치는 월드 행렬에, 메시 id 는 그리는 메시에 든다
 * @details `Component::notifyStateWritten` 이 프로퍼티마다 `onPropertyChanged` 를, 그다음 `onPostLoad` 를 부른다(씬 로드 · 되돌리기와 같은 경로).
 *          직렬화기로 반사 값을 바로 쓰고 알리지 않으면 위치는 트랜스폼 칸에 들어가도 더티가 아니라 월드 행렬 · 화면 · 기즈모가 옛 자리이고,
 *          메시 id 를 붙여 넣어도 옛 메시를 그린다.
 */
SW_TEST_CASE( EditorTransformCommandsTest, PastedValuesAndPresetsReachTheWorldTransformAndTheMesh )
{
    const SerializeContext& context = SerializeContext::getDefault();
    GameObjectManager       manager;
    GameObject*             pSource     = manager.createGameObject( hashed_string( "Source" ) );
    MeshComponent*          pSourceMesh = pSource->addComponent<MeshComponent>();
    GameObject*             pTarget     = manager.createGameObject( hashed_string( "Target" ) );
    MeshComponent*          pTargetMesh = pTarget->addComponent<MeshComponent>();
    SW_ASSERT_TRUE( pSourceMesh != nullptr && pTargetMesh != nullptr );
    const PropertyInfo* pMeshID = pSourceMesh->getTypeInfo()->findPropertyInHierarchy( hashed_string( "_meshID" ) );
    SW_ASSERT_NOT_NULL( pMeshID );
    pTargetMesh->resolveRuntimeMesh();
    manager.flushSceneTransforms();

    // 붙여넣기
    pSourceMesh->setLocalPosition( float3( 3.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( SerializerUtil::applyPropertyText( *pMeshID, pSourceMesh, "Sphere", context ) );
    SW_ASSERT_TRUE( pTargetMesh->getRawMesh() != MeshUtil::acquirePrimitive( "Sphere" ).get() );
    SW_ASSERT_TRUE( EditorTransformCommands::pasteComponentValues( pTargetMesh, XMLSerializer::serialize( pSourceMesh, *pSourceMesh->getTypeInfo() ) ) );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 3.0f, pTargetMesh->getWorldPosition()._x, 1e-4f );
    SW_EXPECT_TRUE( pTargetMesh->getRawMesh() == MeshUtil::acquirePrimitive( "Sphere" ).get() );

    // 프리셋
    pSourceMesh->setLocalPosition( float3( 0.0f, 4.0f, 0.0f ) );
    SW_ASSERT_TRUE( SerializerUtil::applyPropertyText( *pMeshID, pSourceMesh, "Cylinder", context ) );
    const string presetPath = test::makeTempPath( "mesh.preset.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( presetPath, XMLSerializer::serialize( pSourceMesh, *pSourceMesh->getTypeInfo() ) ) );
    SW_ASSERT_TRUE( EditorTransformCommands::loadComponentPreset( pTargetMesh, presetPath ) );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 4.0f, pTargetMesh->getWorldPosition()._y, 1e-4f );
    SW_EXPECT_TRUE( pTargetMesh->getRawMesh() == MeshUtil::acquirePrimitive( "Cylinder" ).get() );
}

/**
 * @brief [EditorTransformCommandsTest] 붙여넣기 · 프리셋 · 새 컴포넌트로 붙여넣기는 값만 옮기고 컴포넌트 이름표는 옮기지 않는다
 * @details 이름표는 저장되는 PROPERTY 라 직렬화기가 값과 함께 싣는다. 그대로 옮기면 붙여 넣은 컴포넌트가 원본의 이름(컴포넌트 키)을 갖게 되어,
 *          같은 오브젝트 안에서 키가 겹치거나 대상을 가리키던 부착 · 오버라이드가 가리킬 곳을 잃는다.
 */
SW_TEST_CASE( EditorTransformCommandsTest, PastingValuesKeepsTheTargetsComponentName )
{
    GameObjectManager manager;
    GameObject*       pSource     = manager.createGameObject( hashed_string( "Source" ) );
    MeshComponent*    pSourceMesh = pSource->addComponent<MeshComponent>();
    GameObject*       pTarget     = manager.createGameObject( hashed_string( "Target" ) );
    MeshComponent*    pTargetMesh = pTarget->addComponent<MeshComponent>();
    SW_ASSERT_TRUE( pSourceMesh != nullptr && pTargetMesh != nullptr );
    pSourceMesh->setComponentName( hashed_string( "Barrel" ) );
    pTargetMesh->setComponentName( hashed_string( "Stock" ) );
    const string copied = XMLSerializer::serialize( pSourceMesh, *pSourceMesh->getTypeInfo() );

    SW_ASSERT_TRUE( EditorTransformCommands::pasteComponentValues( pTargetMesh, copied ) );
    SW_EXPECT_STREQ( "Stock", pTargetMesh->getComponentName().c_str() );

    const string presetPath = test::makeTempPath( "named.preset.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( presetPath, copied ) );
    SW_ASSERT_TRUE( EditorTransformCommands::loadComponentPreset( pTargetMesh, presetPath ) );
    SW_EXPECT_STREQ( "Stock", pTargetMesh->getComponentName().c_str() );

    Component* pPasted = EditorTransformCommands::pasteComponentAsNew( pTarget, "MeshComponent", copied );
    SW_ASSERT_NOT_NULL( pPasted );
    SW_EXPECT_STREQ( "MeshComponent", pPasted->getComponentName().c_str() );
}

/**
 * @brief [EditorTransformCommandsTest] 프리셋 이름 규칙은 하나다 — 이름으로 저장한 것은 목록이 같은 이름으로 읽고, 대화상자는 고른 파일에 그대로 쓴다
 * @details 저장 대화상자가 고른 파일 이름을 다시 이름 규칙에 넘기면 고른 폴더는 버려지고 `MyPreset.preset.xml` 이 프리셋 폴더의
 *          `<타입>_MyPreset.preset.preset.xml` 이 된다. 목록도 접미사 길이를 손으로 세지 않고 같은 규칙으로 이름을 읽는다.
 */
SW_TEST_CASE( EditorTransformCommandsTest, PresetNamesFollowOneRule )
{
    GameObjectManager manager;
    GameObject*       pObj  = manager.createGameObject( hashed_string( "Lamp" ) );
    MeshComponent*    pMesh = pObj->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );

    const string namedFile = EditorTransformCommands::makeComponentPresetFileName( pMesh, "Glossy" );
    SW_EXPECT_STREQ( "Glossy", EditorTransformCommands::getComponentPresetName( pMesh, namedFile ).c_str() );
    SW_EXPECT_STREQ( "Glossy", EditorTransformCommands::getComponentPresetName( pMesh, FileUtil::joinPath( "D:/presets", namedFile ) ).c_str() );
    SW_EXPECT_TRUE( EditorTransformCommands::getComponentPresetName( pMesh, "SceneComponent_Glossy.preset.xml" ).empty() ); // 다른 타입
    SW_EXPECT_TRUE( EditorTransformCommands::getComponentPresetName( pMesh, string( pMesh->getTypeName().c_str() ) + "_.preset.xml" ).empty() );

    const string chosenFolder = test::makeTempDirectory( "chosen_presets" );
    const string chosenPath   = FileUtil::joinPath( chosenFolder, "MyPreset.preset.xml" );
    SW_ASSERT_TRUE( EditorTransformCommands::saveComponentPresetTo( pMesh, chosenPath ) );
    SW_EXPECT_TRUE( FileUtil::exists( chosenPath ) ); // 고른 자리 · 고른 이름 그대로
    SW_ASSERT_TRUE( EditorTransformCommands::saveComponentPresetTo( pMesh, FileUtil::joinPath( chosenFolder, "plain.xml" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::joinPath( chosenFolder, "plain.preset.xml" ) ) );
    SW_EXPECT_TRUE( EditorTransformCommands::loadComponentPreset( pMesh, chosenPath ) );
}

/**
 * @brief [EditorTransformCommandsTest] 바닥 붙이기는 메시의 실제 크기로 잰다 — 평면은 바닥에 눕고, 단위 상자가 아닌 메시도 바닥에 선다
 * @details 오브젝트의 월드 상자(`GameObject::getWorldBox`)로 잰다. "메시면 월드 스케일 × 단위 상자" 로 셈하면 두께 없는 평면이 반 칸(0.5) 뜬다.
 */
SW_TEST_CASE( EditorTransformCommandsTest, SnapToGroundMeasuresTheMeshNotAUnitBox )
{
    GameObjectManager manager;
    GameObject*       pRug  = manager.createGameObject( hashed_string( "Rug" ) );
    MeshComponent*    pMesh = pRug->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMesh( MeshUtil::acquirePrimitive( "Plane" ) );
    pMesh->setLocalPosition( float3( 0.0f, 3.0f, 0.0f ) );
    manager.flushSceneTransforms();

    EditorTransformCommands::snapObjectsToGround( { pRug } );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 0.0f, pMesh->getWorldPosition()._y, 1e-4f );
}
