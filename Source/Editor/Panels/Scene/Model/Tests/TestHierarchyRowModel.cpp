#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Scene/Model/HierarchyRowModel.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

// HierarchyRowModelTest — 계층 창의 행 트리. 펼침을 ImGui 저장소가 아니라 모델이 들고, 접힌 행의 자식은 행 목록에 없으며, 행 높이 위치와 행 번호가 서로 맞는지 씬 밖의 지역 매니저로 본다.

namespace
{
    struct HierarchyRowModelTestInternal
    {
        static sw::GameObject* spawn( sw::GameObjectManager& manager, const utf8* pName )
        {
            sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
            if ( pObject != nullptr )
                (void)pObject->addComponent<sw::SceneComponent>(); // 부모 붙이기는 장면 컴포넌트끼리의 일이다
            return pObject;
        }
    };
} // namespace

/**
 * @brief [HierarchyRowModelTest] 접힌 루트의 자식은 행에 없고, 펼치면 부모 바로 뒤에 들어온다 — 펼침은 루트 · 필터를 다시 짓지 않고 행만 다시 편다
 */
SW_TEST_CASE( HierarchyRowModelTest, ExpansionAddsChildRowsWithoutRebuildingRoots )
{
    using Internal = HierarchyRowModelTestInternal;
    using Kind     = sw::editor::HierarchyRowKind;

    sw::GameObjectManager manager;
    sw::GameObject*       pParent = Internal::spawn( manager, "RowParent" );
    sw::GameObject*       pOther  = Internal::spawn( manager, "RowOther" );
    sw::GameObject*       pChild  = Internal::spawn( manager, "RowChild" );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pOther );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );

    sw::editor::HierarchyRowModel rows;
    rows.refresh( manager, "", 0 );
    SW_EXPECT_TRUE( rows.getVisibleRootCount() == 2u );
    SW_EXPECT_TRUE( rows.getRebuildCount() == 1u );
    SW_EXPECT_FALSE( rows.findObjectRowIndex( pChild->getObjectID() ).has_value() );
    SW_EXPECT_TRUE( rows.findObjectRowIndex( pOther->getObjectID() ).value_or( 99u ) == 1u );

    rows.setExpanded( Kind::Object, pParent->getObjectID(), true );
    rows.refresh( manager, "", 0 );
    SW_EXPECT_TRUE( rows.getRebuildCount() == 1u );
    SW_EXPECT_TRUE( rows.findObjectRowIndex( pChild->getObjectID() ).value_or( 99u ) == 1u );
    SW_EXPECT_TRUE( rows.findObjectRowIndex( pOther->getObjectID() ).value_or( 99u ) == 2u );
    const sw::optional<uint32> childRow = rows.findRowOfObject( pChild->getObjectID() );
    SW_REQUIRE_TRUE( childRow.has_value() );
    SW_EXPECT_TRUE( rows.getRows()[*childRow]._depth == 1u );
    SW_EXPECT_TRUE( rows.getRows()[*childRow]._parentRow == 0u );

    rows.setExpanded( Kind::Object, pParent->getObjectID(), false );
    rows.refresh( manager, "", 0 );
    SW_EXPECT_FALSE( rows.findObjectRowIndex( pChild->getObjectID() ).has_value() );
}

/**
 * @brief [HierarchyRowModelTest] 행 높이 위치(오브젝트 행 · 컴포넌트 행 높이가 다르다)와 그 위치의 행 번호가 서로 맞는다 — 창이 화면 밖 행을 묶음으로 건너뛰는 근거다
 */
SW_TEST_CASE( HierarchyRowModelTest, RowOffsetAndRowAtOffsetAgree )
{
    using Internal = HierarchyRowModelTestInternal;
    using Kind     = sw::editor::HierarchyRowKind;

    sw::GameObjectManager manager;
    sw::GameObject*       pParent = Internal::spawn( manager, "OffsetParent" );
    sw::GameObject*       pLast   = Internal::spawn( manager, "OffsetLast" );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pLast );

    sw::editor::HierarchyRowModel rows;
    rows.setExpanded( Kind::Object, pParent->getObjectID(), true ); // 컴포넌트 행이 오브젝트 행 사이에 든다
    rows.refresh( manager, "", 0 );
    const sw::optional<uint32> lastRow = rows.findRowOfObject( pLast->getObjectID() );
    SW_REQUIRE_TRUE( lastRow.has_value() );
    SW_REQUIRE_TRUE( *lastRow >= 2u ); // 부모 · 그 장면 컴포넌트 · 마지막 루트

    const sw::editor::HierarchyRowMetrics stride{ 10.0f, 4.0f };
    const float32                         lastOffset    = rows.computeRowOffset( *lastRow, stride );
    const uint32                          componentRows = *lastRow - rows.countObjectRowsBefore( *lastRow );
    SW_EXPECT_TRUE( componentRows >= 1u );
    SW_EXPECT_TRUE( lastOffset == static_cast<float32>( rows.countObjectRowsBefore( *lastRow ) ) * 10.0f + static_cast<float32>( componentRows ) * 4.0f );
    SW_EXPECT_TRUE( rows.findRowAtOffset( lastOffset, stride ) == *lastRow );
    SW_EXPECT_TRUE( rows.findRowAtOffset( lastOffset - 0.5f, stride ) == *lastRow - 1u );
    SW_EXPECT_TRUE( rows.findRowAtOffset( -100.0f, stride ) == 0u );
    SW_EXPECT_TRUE( rows.findRowAtOffset( 1.0e6f, stride ) == *lastRow );
}

/**
 * @brief [HierarchyRowModelTest] 필터는 걸린 자식의 조상 루트를 남기고, 숨길 루트(에디터 카메라)는 루트 수에서 빠진다
 */
SW_TEST_CASE( HierarchyRowModelTest, FilterKeepsAncestorsAndHiddenRootIsLeftOut )
{
    using Internal = HierarchyRowModelTestInternal;

    sw::GameObjectManager manager;
    sw::GameObject*       pParent = Internal::spawn( manager, "FilterParent" );
    sw::GameObject*       pOther  = Internal::spawn( manager, "FilterOther" );
    sw::GameObject*       pChild  = Internal::spawn( manager, "FilterNeedle" );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pOther );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );

    sw::editor::HierarchyRowModel rows;
    rows.refresh( manager, "Needle", 0 );
    SW_EXPECT_TRUE( rows.isFilterActive() );
    SW_EXPECT_TRUE( rows.getVisibleRootCount() == 1u );
    SW_EXPECT_TRUE( rows.findObjectRowIndex( pParent->getObjectID() ).has_value() );
    SW_EXPECT_FALSE( rows.findObjectRowIndex( pOther->getObjectID() ).has_value() );

    rows.refresh( manager, "", pOther->getObjectID() );
    SW_EXPECT_FALSE( rows.isFilterActive() );
    SW_EXPECT_TRUE( rows.getVisibleRootCount() == 1u );
    SW_EXPECT_FALSE( rows.findObjectRowIndex( pOther->getObjectID() ).has_value() );
}
