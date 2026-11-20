#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/InspectorEditRecorder.h"
#include "Editor/Panels/Inspector/Model/PropertyEditStep.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/TypeInfo.h"

// InspectorEditRecorderTest — 인스펙터 프로퍼티 편집 구간(감사 E-7): 찍는 때는 모델의 첫 쓰기 직전이고, 단위는 (오브젝트 id, 컴포넌트 키, 프로퍼티 경로, 값 글)이다.
// 구간 표(`PropertyEditSpanSet`)는 서비스를 모르므로 위젯 상태 · 쓰기 · 마감 차례를 그대로 몬다. 씬 밖 지역 매니저로 본다(되돌리기 스택 기록은 시나리오 `editor/undocoverage` 의 길이다).

namespace
{
    struct InspectorEditRecorderTestInternal
    {
        /** @brief 대상 하나(경로 @p pPath)의 구간입니다 — 구간 표의 차례만 본다. */
        static sw::editor::PropertyEditSpan makeSpan( const utf8* pPath )
        {
            sw::editor::PropertyEditSpan span;
            span._listTarget.push_back( sw::editor::PropertyEditTarget{ 1, 0 } );
            span._listPath.push_back( sw::string{ pPath } );
            span._listBefore.push_back( sw::string{ "0" } );
            span._label = "Edit Test";
            return span;
        }

        static const sw::PropertyInfo* findPositionProperty( const sw::SceneComponent& component )
        {
            const sw::TypeInfo* pType = component.getTypeInfo();
            return pType != nullptr ? pType->findPropertyInHierarchy( sw::hashed_string( "_localPosition" ) ) : nullptr;
        }
    };
} // namespace

/**
 * @brief [InspectorEditRecorderTest] 위젯을 누르기만 하고 나오면 아무것도 남지 않는다 — 결함: 활성화가 선택 전체를 찍고, 닫기는 편집했을 때뿐이라 스냅샷이 세션 끝까지 남았다
 */
SW_TEST_CASE( InspectorEditRecorderTest, DeactivateWithoutEditDropsPending )
{
    sw::editor::PropertyEditSpanSet spans;
    spans.touch( 7 ); // 누른 프레임 — 쓰기가 없으니 구간도 없다
    SW_EXPECT_FALSE( spans.isHeld( 7 ) );
    sw::vector<sw::editor::PropertyEditSpan> listClosed;
    spans.collectClosed( listClosed );
    SW_EXPECT_TRUE( listClosed.empty() );
    SW_EXPECT_FALSE( spans.isHeld( 7 ) );
}

/**
 * @brief [InspectorEditRecorderTest] 다중 선택의 끌기 한 번은 대상마다 그 프로퍼티 하나만 찍은 구간 하나다 — 끄는 동안의 쓰기는 같은 구간을 잇고, 풀리면 마감에 닫힌다
 */
SW_TEST_CASE( InspectorEditRecorderTest, EditRecordsOnePropertyPerTarget )
{
    using Internal = InspectorEditRecorderTestInternal;
    sw::GameObjectManager      manager;
    sw::vector<sw::Component*> listComponent;
    for ( const utf8* pName : { "First", "Second", "Third" } )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        SW_REQUIRE_NOT_NULL( pObject );
        sw::SceneComponent* pScene = pObject->addComponent<sw::SceneComponent>();
        SW_REQUIRE_NOT_NULL( pScene );
        listComponent.push_back( pScene );
    }
    sw::SceneComponent*     pPrimary = static_cast<sw::SceneComponent*>( listComponent[0] );
    const sw::PropertyInfo* pProp    = Internal::findPositionProperty( *pPrimary );
    SW_REQUIRE_NOT_NULL( pProp );
    const sw::Delegate<void( sw::vector<sw::Component*>& )> collect =
        SW_DELEGATE_LAMBDA( sw::Delegate<void( sw::vector<sw::Component*>& )>, [&listComponent]( sw::vector<sw::Component*>& outList )
    { outList.insert( outList.end(), listComponent.begin(), listComponent.end() ); } );

    sw::editor::PropertyEditSpan span = sw::editor::PropertyEditSpan::captureProperty( *pPrimary->getOwner(), pPrimary, *pProp, collect, "Edit Position" );
    SW_REQUIRE_EQUAL( span._listTarget.size(), size_t{ 3 } );
    SW_REQUIRE_EQUAL( span._listBefore.size(), size_t{ 3 } );
    SW_REQUIRE_EQUAL( span._listPath.size(), size_t{ 3 } );
    for ( size_t index = 0; index < listComponent.size(); ++index )
    {
        const sw::editor::PropertyEditTarget& target = span._listTarget[index];
        SW_EXPECT_EQUAL( target._objectID, listComponent[index]->getOwner()->getObjectID() );
        SW_EXPECT_NOT_EQUAL( target._componentLocalID, uint64{ 0 } );
        SW_EXPECT_TRUE( span._listPath[index] == "_localPosition" );
        SW_EXPECT_FALSE( span._listBefore[index].empty() );
    }

    // 끌기: 첫 쓰기가 연 행(키 5)의 구간을 행이 활성인 프레임들이 잇고, 활성이 아닌(놓은) 프레임의 마감이 닫는다.
    sw::editor::PropertyEditSpanSet spans;
    SW_EXPECT_FALSE( spans.isHeld( 5 ) );
    spans.open( 5, std::move( span ) ); // 첫 쓰기
    spans.touch( 5 );                   // 그 행을 그리는 동안 위젯이 활성이었다
    SW_EXPECT_TRUE( spans.isHeld( 5 ) );
    sw::vector<sw::editor::PropertyEditSpan> listClosed;
    spans.collectClosed( listClosed );
    SW_EXPECT_TRUE( listClosed.empty() );
    SW_EXPECT_TRUE( spans.isHeld( 5 ) ); // 끄는 프레임의 쓰기는 같은 구간을 잇는다
    spans.touch( 5 );
    spans.collectClosed( listClosed );
    SW_EXPECT_TRUE( listClosed.empty() );
    spans.touch( 5 ); // 멈춰 쥐고 있는 프레임 — 쓰기는 없지만 행이 활성이다
    spans.collectClosed( listClosed );
    SW_EXPECT_TRUE( listClosed.empty() );
    spans.collectClosed( listClosed ); // 놓은 프레임
    SW_REQUIRE_EQUAL( listClosed.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listClosed[0]._listTarget.size(), size_t{ 3 } );
    SW_EXPECT_FALSE( spans.isHeld( 5 ) );
}

/**
 * @brief [InspectorEditRecorderTest] 되돌리기는 값 글을 모델의 쓰기 길로 쓴다 — 결함이면 `onPropertyChanged` 가 빠져 트랜스폼 캐시가 옛 월드 위치를 낸다
 */
SW_TEST_CASE( InspectorEditRecorderTest, UndoRestoresValueThroughModelPath )
{
    using Internal = InspectorEditRecorderTestInternal;
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Mover" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::SceneComponent* pScene = pObject->addComponent<sw::SceneComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_NOT_NULL( Internal::findPositionProperty( *pScene ) );
    static constexpr sw::string_view kPath = "_localPosition";

    const sw::editor::PropertyEditTarget target = sw::editor::PropertyEditTarget::make( *pObject, pScene );
    sw::string                           beforeText;
    SW_REQUIRE_TRUE( target.readValueText( *pObject, kPath, beforeText ) );

    pScene->setLocalPosition( sw::float3{ 1.0f, 2.0f, 3.0f } );
    SW_EXPECT_NEAR( 2.0f, pScene->getWorldPosition()._y, 1.0e-5f ); // 월드 캐시를 채운다
    sw::string afterText;
    SW_REQUIRE_TRUE( target.readValueText( *pObject, kPath, afterText ) );
    SW_EXPECT_FALSE( afterText == beforeText );

    SW_REQUIRE_TRUE( target.applyTo( *pObject, kPath, beforeText ) );
    SW_EXPECT_NEAR( 0.0f, pScene->getWorldPosition()._y, 1.0e-5f );
    SW_REQUIRE_TRUE( target.applyTo( *pObject, kPath, afterText ) );
    SW_EXPECT_NEAR( 2.0f, pScene->getWorldPosition()._y, 1.0e-5f );

    // 없는 컴포넌트 지역 id 는 건너뛴다(그 사이 지운 컴포넌트).
    sw::editor::PropertyEditTarget gone = target;
    gone._componentLocalID              = 0xdeadbeefull;
    SW_EXPECT_FALSE( gone.applyTo( *pObject, kPath, beforeText ) );
}

/**
 * @brief [InspectorEditRecorderTest] 해제를 받지 못한 구간(위젯이 사라졌다)은 활성이 아닌 프레임의 마감이 닫는다 — 한 번 쓰기(팝업 · 콤보)는 그 프레임에 닫힌다
 * @details 설계는 "다음 gather" 였으나 한 그리드가 한 프레임에 대상 여럿을 모으므로(카드마다) 모으기로는 다른 카드의 활성 위젯과 가릴 수 없다 — 그리드의 프레임 끝(`endFrame`)이 닫는다.
 */
SW_TEST_CASE( InspectorEditRecorderTest, OrphanedSpanClosesAtFrameEnd )
{
    using Internal = InspectorEditRecorderTestInternal;
    sw::editor::PropertyEditSpanSet          spans;
    sw::vector<sw::editor::PropertyEditSpan> listClosed;

    spans.open( 9, Internal::makeSpan( "_value" ) );
    spans.touch( 9 );
    spans.collectClosed( listClosed );
    SW_EXPECT_TRUE( listClosed.empty() );
    // 다음 프레임: 위젯이 그려지지 않았다(선택이 바뀌었거나 머리를 접었다).
    spans.collectClosed( listClosed );
    SW_EXPECT_EQUAL( listClosed.size(), size_t{ 1 } );
    SW_EXPECT_FALSE( spans.isHeld( 9 ) );

    // 활성 위젯 없이 쓴 편집(팝업 · 콤보 · 붙여 넣기)은 그 프레임의 마감이 닫는다. 다른 행에 쓰면 열린 구간은 바로 닫힌다 — 묶인 구간은 하나다.
    spans.open( 3, Internal::makeSpan( "_value" ) );
    spans.open( 4, Internal::makeSpan( "_other" ) );
    SW_EXPECT_FALSE( spans.isHeld( 3 ) );
    SW_EXPECT_TRUE( spans.isHeld( 4 ) );
    spans.collectClosed( listClosed );
    SW_EXPECT_EQUAL( listClosed.size(), size_t{ 2 } );
    SW_EXPECT_FALSE( spans.isHeld( 4 ) );
}
