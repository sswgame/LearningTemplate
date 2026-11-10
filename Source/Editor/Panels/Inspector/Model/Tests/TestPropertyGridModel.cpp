#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"

// PropertyGridModelTest — 프로퍼티 그리드 대상이 값(범위 · 오브젝트 id · 컴포넌트 id)이라 그리는 사이 사라진 대상은 묶이지 않고, 씬 밖 대상은 주소 그대로 묶인다.

/**
 * @brief [PropertyGridModelTest] 씬 밖 대상은 인스턴스 · 타입 그대로 묶이고, 대상 자신(즐겨찾기 키의 주인)은 씬 대상만이다
 */
SW_TEST_CASE( PropertyGridModelTest, FreeTargetBindsItsInstanceAndIsNotAFavoriteOwner )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Owner" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::TagComponent* pTag = pObject->addComponent<sw::TagComponent>();
    SW_REQUIRE_NOT_NULL( pTag );

    sw::editor::PropertyGridModel        model;
    sw::editor::EditorPropertyGridTarget target{};
    target._pInstance = pTag;
    target._pType     = pTag->getTypeInfo();
    SW_REQUIRE_TRUE( model.beginTarget( target ) );
    SW_EXPECT_TRUE( model.getInstance() == static_cast<void*>( pTag ) );
    SW_EXPECT_TRUE( model.getType() == pTag->getTypeInfo() );
    SW_EXPECT_FALSE( model.isTargetInstance( pTag ) );
    SW_EXPECT_FALSE( model.isMultiEdit() );
    SW_EXPECT_TRUE( model.findEditOwnerManager() == nullptr );
    model.endTarget();
    SW_EXPECT_TRUE( model.getInstance() == nullptr );
}

/**
 * @brief [PropertyGridModelTest] 인스턴스나 타입이 빈 씬 밖 대상, 찾을 수 없는 씬 대상은 묶이지 않는다 — 매달린 포인터 대신 그리지 않는다
 */
SW_TEST_CASE( PropertyGridModelTest, MissingTargetDoesNotBind )
{
    sw::editor::PropertyGridModel        model;
    sw::editor::EditorPropertyGridTarget empty{};
    SW_EXPECT_FALSE( model.beginTarget( empty ) );
    SW_EXPECT_TRUE( model.getInstance() == nullptr );

    // 어떤 씬에도 없는 id — 미리보기 범위는 겨눈 미리보기가 없으면 씬이 없다.
    const sw::editor::EditorPropertyGridTarget gone = sw::editor::EditorPropertyGridTarget::makeSceneTarget( sw::editor::EditorPropertyGridScope::FocusedPreview, 0xFFFFFFFF0ull, 0 );
    SW_EXPECT_FALSE( model.beginTarget( gone ) );
    SW_EXPECT_TRUE( model.getInstance() == nullptr );
    SW_EXPECT_TRUE( sw::editor::PropertyGridModel::findSceneObject( sw::editor::EditorPropertyGridScope::None, 1 ) == nullptr );
}
