/**
 * @file TestEditorSelectionBatch.cpp
 * @brief 여러 개를 고르는 길(`EditorSelection::selectObjects`)이 알림을 한 번만 보내고, 순서 목록과 포함 색인이 함께 맞는지 미리보기 씬에 묶은 선택으로 봅니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace
{
    struct EditorSelectionBatchTestInternal
    {
        /** @brief @p scene 에 오브젝트 @p count 개를 만들어 만든 순서로 돌려줍니다. */
        static sw::vector<sw::GameObject*> spawn( sw::Scene& scene, size_t count )
        {
            sw::vector<sw::GameObject*> listObject;
            for ( size_t index = 0; index < count; ++index )
            {
                listObject.push_back( scene.getObjectManager()->createGameObject( sw::hashed_string( "BatchProbe" ) ) );
            }
            return listObject;
        }

        /** @brief 선택의 순서 목록이 @p listExpected 와 같은지 봅니다. */
        static bool matchesInOrder( const sw::editor::EditorSelection& selection, const sw::vector<sw::GameObject*>& listExpected )
        {
            const sw::vector<sw::GameObjectHandle>& listHandle = selection.getSelectedHandles();
            if ( listHandle.size() != listExpected.size() )
                return false;
            for ( size_t index = 0; index < listHandle.size(); ++index )
            {
                if ( listHandle[index] != listExpected[index]->getHandle() || selection.hasObject( listExpected[index] ) == false )
                    return false;
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [EditorSelectionTest] 많이 골라도 알림은 한 번이고 선택한 순서를 지킨다. 같은 오브젝트를 두 번 넣어도 한 칸이다(감사 E-5 — 전체 선택 제곱)
 */
SW_TEST_CASE( EditorSelectionTest, SelectManyNotifiesOnce )
{
    using Internal = EditorSelectionBatchTestInternal;

    sw::Scene                   scene( "SelectionBatchProbe" );
    sw::CommandStack            stack;
    sw::editor::EditorSelection selection;
    selection.bindPreviewScope( scene, stack );

    const sw::vector<sw::GameObject*> listObject = Internal::spawn( scene, 64 );
    for ( const sw::GameObject* pObject : listObject )
    {
        SW_REQUIRE_NOT_NULL( pObject );
    }
    uint32 notifyCount             = 0;
    selection.onSelectionChanged() = sw::Delegate<void()>( [&notifyCount]()
    { ++notifyCount; } );

    sw::vector<sw::GameObject*> listPicked = listObject;
    listPicked.push_back( listObject[3] ); // 겹친 칸
    listPicked.push_back( nullptr );       // 빈 칸은 버린다
    selection.selectObjects( listPicked, sw::editor::SelectionMode::Replace );
    SW_EXPECT_EQUAL( notifyCount, 1u );
    SW_EXPECT_TRUE( Internal::matchesInOrder( selection, listObject ) );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == listObject[0] );

    // 이미 든 것을 더하면 그대로다
    selection.selectObjects( { listObject[5], listObject[6] }, sw::editor::SelectionMode::Add );
    SW_EXPECT_EQUAL( notifyCount, 2u );
    SW_EXPECT_TRUE( Internal::matchesInOrder( selection, listObject ) );
}

/**
 * @brief [EditorSelectionTest] 빼기 · 뒤집기는 남은 순서를 지키고 포함 색인이 목록과 같이 움직인다 — 한 호출에서 뺐다가 다시 넣어도 칸이 겹치지 않는다
 */
SW_TEST_CASE( EditorSelectionTest, RemoveAndToggleKeepIndexInSync )
{
    using Internal = EditorSelectionBatchTestInternal;

    sw::Scene                   scene( "SelectionToggleProbe" );
    sw::CommandStack            stack;
    sw::editor::EditorSelection selection;
    selection.bindPreviewScope( scene, stack );

    const sw::vector<sw::GameObject*> listObject = Internal::spawn( scene, 8 );
    for ( const sw::GameObject* pObject : listObject )
    {
        SW_REQUIRE_NOT_NULL( pObject );
    }
    selection.selectObjects( listObject, sw::editor::SelectionMode::Replace );

    // 짝수 칸을 뺀다
    sw::vector<sw::GameObject*> listEven;
    sw::vector<sw::GameObject*> listOdd;
    for ( size_t index = 0; index < listObject.size(); ++index )
    {
        ( index % 2 == 0 ? listEven : listOdd ).push_back( listObject[index] );
    }
    selection.selectObjects( listEven, sw::editor::SelectionMode::Remove );
    SW_EXPECT_TRUE( Internal::matchesInOrder( selection, listOdd ) );
    SW_EXPECT_FALSE( selection.hasObject( listObject[0] ) );

    // 뒤집기: 0 은 들어가 끝에 붙고, 1 은 빠진다. 3 을 두 번 뒤집으면 제자리다.
    selection.selectObjects( { listObject[0], listObject[1], listObject[3], listObject[3] }, sw::editor::SelectionMode::Toggle );
    SW_EXPECT_TRUE( Internal::matchesInOrder( selection, { listObject[3], listObject[5], listObject[7], listObject[0] } ) );
    SW_EXPECT_FALSE( selection.hasObject( listObject[1] ) );

    // 한 개 고르기도 같은 색인을 쓴다
    selection.selectObject( listObject[7], sw::editor::SelectionMode::Remove );
    SW_EXPECT_FALSE( selection.hasObject( listObject[7] ) );
    selection.selectObject( listObject[2], sw::editor::SelectionMode::Replace );
    SW_EXPECT_TRUE( Internal::matchesInOrder( selection, { listObject[2] } ) );
    SW_EXPECT_FALSE( selection.hasObject( listObject[3] ) );

    selection.clearObjectSelection();
    SW_EXPECT_FALSE( selection.hasObject( listObject[2] ) );
}
