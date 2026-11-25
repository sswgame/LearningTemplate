#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorEditRecorder.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditRequest.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditStep.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyGridViewModel.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"

// PropertyEditRequestTest — 인스펙터 프로퍼티 칸 편집 요청(경로 · 종류 · 새 값): 값만 들고 적용할 때 대상 인스턴스에 경로를 풀어 쓴다. 끌기는 프레임마다 요청이고
// 되돌리기는 편집 구간 하나다. 씬 밖 지역 매니저로 본다(게임 단계로 넘기기 · 스택 기록은 시나리오 `editor/undocoverage` 의 길이다).

namespace
{
    struct PropertyEditRequestTestInternal
    {
        static sw::editor::PropertyEditRequest makeRequest( const utf8* pRoot, sw::editor::PropertyEditKind kind )
        {
            sw::editor::PropertyEditRequest request;
            request._path._root = sw::hashed_string( pRoot );
            request._kind       = kind;
            return request;
        }

        static void appendField( sw::editor::PropertyEditRequest& request, const utf8* pName )
        {
            sw::editor::PropertyPathNode node{};
            node._step = sw::editor::PropertyPathStep::Field;
            node._name = sw::hashed_string( pName );
            request._path._listNode.push_back( node );
        }

        static bool apply( const sw::editor::PropertyEditRequest& request, sw::Component& component )
        {
            const sw::TypeInfo* pType = component.getTypeInfo();
            return pType != nullptr && request.applyTo( sw::MutableObjectAddress{ &component }, *pType, sw::editor::PropertyGridViewModel::getTypeRegistry() );
        }
    };
} // namespace

/**
 * @brief [PropertyEditRequestTest] 요청은 구조체 필드 경로를 대상에서 다시 풀어 쓴다 — 숫자는 허용 범위(`Max`)로 잘린다. 결함이면 행 번호에 묶인 주소로 써 모으기 뒤에 낡는다
 */
SW_TEST_CASE( PropertyEditRequestTest, NestedFieldPathIsResolvedOnApply )
{
    using Internal = PropertyEditRequestTestInternal;
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Camera" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::CameraComponent* pCamera = pObject->addComponent<sw::CameraComponent>();
    SW_REQUIRE_NOT_NULL( pCamera );

    sw::editor::PropertyEditRequest width = Internal::makeRequest( "_renderOutput", sw::editor::PropertyEditKind::SetNumber );
    Internal::appendField( width, "_renderTextureWidth" );
    width._number = 100000.0;
    SW_EXPECT_TRUE( Internal::apply( width, *pCamera ) );
    SW_EXPECT_EQUAL( 8192u, pCamera->getRenderOutput()._renderTextureWidth );

    sw::editor::PropertyEditRequest shadows = Internal::makeRequest( "_renderOutput", sw::editor::PropertyEditKind::SetValue );
    Internal::appendField( shadows, "_bShadows" );
    shadows._value = sw::TaskValue{ false };
    SW_EXPECT_TRUE( Internal::apply( shadows, *pCamera ) );
    SW_EXPECT_FALSE( pCamera->getRenderOutput()._bShadows );
}

/**
 * @brief [PropertyEditRequestTest] 풀리지 않는 경로(없는 프로퍼티 · 구조체가 아닌 값의 필드 · 컨테이너가 아닌 값의 원소)는 아무것도 쓰지 않고 false 다
 */
SW_TEST_CASE( PropertyEditRequestTest, UnresolvedPathWritesNothing )
{
    using Internal = PropertyEditRequestTestInternal;
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Camera" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::CameraComponent* pCamera = pObject->addComponent<sw::CameraComponent>();
    SW_REQUIRE_NOT_NULL( pCamera );
    const uint32 widthBefore = pCamera->getRenderOutput()._renderTextureWidth;

    sw::editor::PropertyEditRequest missing = Internal::makeRequest( "_noSuchProperty", sw::editor::PropertyEditKind::SetNumber );
    missing._number                         = 1.0;
    SW_EXPECT_FALSE( Internal::apply( missing, *pCamera ) );

    sw::editor::PropertyEditRequest fieldOfNumber = Internal::makeRequest( "_fovY", sw::editor::PropertyEditKind::SetNumber );
    Internal::appendField( fieldOfNumber, "_renderTextureWidth" );
    fieldOfNumber._number = 1.0;
    SW_EXPECT_FALSE( Internal::apply( fieldOfNumber, *pCamera ) );

    sw::editor::PropertyEditRequest elementOfStruct = Internal::makeRequest( "_renderOutput", sw::editor::PropertyEditKind::SetNumber );
    sw::editor::PropertyPathNode    element{};
    element._step    = sw::editor::PropertyPathStep::Element;
    element._ordinal = 0;
    elementOfStruct._path._listNode.push_back( element );
    elementOfStruct._number = 1.0;
    SW_EXPECT_FALSE( Internal::apply( elementOfStruct, *pCamera ) );
    SW_EXPECT_EQUAL( widthBefore, pCamera->getRenderOutput()._renderTextureWidth );
}

/**
 * @brief [PropertyEditRequestTest] 끌기는 프레임마다 요청이고 되돌리기는 한 단계다 — 구간은 첫 요청 전에 한 번 찍고, 놓은 프레임에 닫혀 처음 값 → 마지막 값 하나를 남긴다.
 *        그 단계의 되돌리기 · 다시 하기는 값 글을 모델의 쓰기 길로 쓴다
 */
SW_TEST_CASE( PropertyEditRequestTest, DragIsOneUndoStep )
{
    using Internal = PropertyEditRequestTestInternal;
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Mover" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::SceneComponent* pScene = pObject->addComponent<sw::SceneComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    const sw::TypeInfo* pType = pScene->getTypeInfo();
    SW_REQUIRE_NOT_NULL( pType );
    const sw::PropertyInfo* pProp = pType->findPropertyInHierarchy( sw::hashed_string( "_localPosition" ) );
    SW_REQUIRE_NOT_NULL( pProp );
    static constexpr sw::string_view kPath = "_localPosition";
    static constexpr uint64          kKey  = 11;

    // 첫 요청 직전에 구간을 연다(`PropertyGridViewModel::ensureEditSpan` 와 같은 차례).
    sw::editor::PropertyEditSpanSet spans;
    spans.open( kKey, sw::editor::PropertyEditSpan::captureProperty( *pObject, pScene, *pProp, {}, "Edit Position" ) );
    sw::vector<sw::editor::PropertyEditSpan> listClosed;
    for ( const float32 x : { 1.0f, 2.0f, 3.0f } )
    {
        sw::editor::PropertyEditRequest drag = Internal::makeRequest( "_localPosition", sw::editor::PropertyEditKind::SetValue );
        drag._value                          = sw::TaskValue{
            sw::float3{ x, 0.0f, 0.0f }
        };
        SW_EXPECT_TRUE( Internal::apply( drag, *pScene ) );
        SW_EXPECT_TRUE( spans.isHeld( kKey ) ); // 끄는 프레임의 요청은 구간을 다시 찍지 않는다
        spans.touch( kKey );
        spans.collectClosed( listClosed );
        SW_EXPECT_TRUE( listClosed.empty() );
    }
    spans.collectClosed( listClosed ); // 놓은 프레임
    SW_REQUIRE_EQUAL( listClosed.size(), size_t{ 1 } );
    const sw::editor::PropertyEditSpan& span = listClosed[0];
    SW_REQUIRE_EQUAL( span._listTarget.size(), size_t{ 1 } );

    const sw::editor::PropertyEditTarget& target = span._listTarget[0];
    sw::string                            afterText;
    SW_REQUIRE_TRUE( target.readValueText( *pObject, kPath, afterText ) );
    SW_EXPECT_FALSE( afterText == span._listBefore[0] ); // 처음 값 → 마지막 값 한 쌍

    SW_REQUIRE_TRUE( target.applyTo( *pObject, kPath, span._listBefore[0] ) ); // 되돌리기
    SW_EXPECT_NEAR( 0.0f, pScene->getLocalPosition()._x, 1.0e-5f );
    SW_REQUIRE_TRUE( target.applyTo( *pObject, kPath, afterText ) ); // 다시 하기
    SW_EXPECT_NEAR( 3.0f, pScene->getLocalPosition()._x, 1.0e-5f );
}
