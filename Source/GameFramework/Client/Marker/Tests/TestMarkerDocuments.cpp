// MarkerDocumentTest — 화면 마커의 내용: 목표 마커가 엔진 문서에 칸(색 · 크기 · 글자 크기 · 이름)을 옮기고 필요한 위젯이 없는 문서를 거절한다, 데미지 숫자는 컴포넌트 기본값의 수명이 다하면 사라진다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/UI/Document/UIBindingDef.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Client/Marker/DamageNumberComponent.h"
#include "GameFramework/Client/Marker/ObjectiveMarkerComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct ObjectiveMarkerComponentTestAccess
    {
        [[nodiscard]] static bool onDocumentCreated( ObjectiveMarkerComponent& marker, Widget& content ) { return marker.onDocumentCreated( content ); }
        static const float4&      getArrowColor( const ObjectiveMarkerComponent& marker ) { return marker._arrowColor; }
        static const float2&      getArrowSize( const ObjectiveMarkerComponent& marker ) { return marker._arrowSize; }
        static float32            getFontSize( const ObjectiveMarkerComponent& marker ) { return marker._fontSize; }
    };
} // namespace sw

using namespace sw;

/** @brief [MarkerDocumentTest] 목표 마커는 엔진 문서의 막대 · 이름 · 거리에 자기 칸을 옮기고, 막대는 숨긴 채 시작한다 */
SW_TEST_CASE( MarkerDocumentTest, ObjectiveMarkerFillsTheEngineDocument )
{
    using Access = ObjectiveMarkerComponentTestAccess;
    GameObjectManager         manager;
    ObjectiveMarkerComponent* pMarker = manager.createGameObject( hashed_string( "Objective" ) )->addComponent<ObjectiveMarkerComponent>();
    SW_REQUIRE_NOT_NULL( pMarker );
    pMarker->setLabel( "Reach the mill" );

    UIDocumentCache                                   cache;
    const expected<shared_ptr<const UIDocumentAsset>> document = cache.acquire( game::getSubsystem<EngineDefaultAssets>()._uiObjectiveMarker.get() );
    SW_REQUIRE_TRUE( document.has_value() );
    vector<UIBindingDef>     listBinding;
    const unique_ptr<Widget> content = UIDocumentLoader::instantiate( **document, cache, ResourceTagSet{}, listBinding ).value_or( nullptr );
    SW_REQUIRE_NOT_NULL( content.get() );
    SW_EXPECT_TRUE( listBinding.empty() );
    SW_REQUIRE_TRUE( Access::onDocumentCreated( *pMarker, *content ) );

    const BorderPanel* pArrow    = castTo<BorderPanel>( content->findDescendant( hashed_string( "Arrow" ) ) );
    const TextWidget*  pLabel    = castTo<TextWidget>( content->findDescendant( hashed_string( "Label" ) ) );
    const TextWidget*  pDistance = castTo<TextWidget>( content->findDescendant( hashed_string( "Distance" ) ) );
    SW_REQUIRE_TRUE( pArrow != nullptr && pLabel != nullptr && pDistance != nullptr );
    SW_EXPECT_TRUE( pArrow->getVisibility() == WidgetVisibility::Hidden );
    SW_EXPECT_TRUE( pArrow->getBackground()._color == Access::getArrowColor( *pMarker ) );
    SW_EXPECT_EQUAL( Access::getArrowSize( *pMarker )._x, pArrow->getLayoutSlot()._widthOverride );
    SW_EXPECT_EQUAL( Access::getArrowSize( *pMarker )._y, pArrow->getLayoutSlot()._heightOverride );
    SW_EXPECT_EQUAL( string( "Reach the mill" ), pLabel->getText() );
    SW_EXPECT_TRUE( pLabel->isLocalized() );
    SW_EXPECT_FALSE( pDistance->isLocalized() );
    SW_EXPECT_EQUAL( Access::getFontSize( *pMarker ), pLabel->getTextStyle()._fontSize );
    SW_EXPECT_EQUAL( Access::getFontSize( *pMarker ), pDistance->getTextStyle()._fontSize );
}

/** @brief [MarkerDocumentTest] 필요한 위젯이 없는 문서는 이유를 남기고 거절한다 */
SW_TEST_CASE( MarkerDocumentTest, ObjectiveMarkerRefusesADocumentWithoutItsWidgets )
{
    SW_EXPECT_LOG( "ObjectiveMarkerComponent.DocumentNeeds" );
    GameObjectManager         manager;
    ObjectiveMarkerComponent* pMarker = manager.createGameObject( hashed_string( "Objective" ) )->addComponent<ObjectiveMarkerComponent>();
    SW_REQUIRE_NOT_NULL( pMarker );
    BoxPanel content;
    SW_EXPECT_FALSE( ObjectiveMarkerComponentTestAccess::onDocumentCreated( *pMarker, content ) );
}

/** @brief [MarkerDocumentTest] 새 데미지 숫자는 컴포넌트 기본값으로 떠오르고, 수명이 다하면 오브젝트가 지워진다 */
SW_TEST_CASE( MarkerDocumentTest, DamageNumberExpiresWithItsDefaultLifeTime )
{
    SW_EXPECT_LOG( "DamageNumberComponent.DamageNumberNoWidgetComponent" ); // 마커 없이 수명만 본다
    GameObjectManager      manager;
    GameObject*            pObject = manager.createGameObject( hashed_string( "Number" ) );
    DamageNumberComponent* pNumber = pObject->addComponent<DamageNumberComponent>();
    SW_REQUIRE_NOT_NULL( pNumber );
    SW_EXPECT_TRUE( pNumber->getFloatSpeed() > 0.0f );
    const float32 lifeTime = pNumber->getLifeTime();
    SW_REQUIRE_TRUE( lifeTime > 0.0f );
    const GameObjectHandle handle = pObject->getHandle();
    manager.beginPlay();
    manager.update( lifeTime * 0.5f );
    SW_EXPECT_NOT_NULL( manager.resolveGameObject( handle ) );
    manager.update( lifeTime );
    manager.update( lifeTime );
    SW_EXPECT_NULL( manager.resolveGameObject( handle ) );
}
