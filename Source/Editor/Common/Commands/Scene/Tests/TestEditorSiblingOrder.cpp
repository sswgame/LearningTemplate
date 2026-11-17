#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"
#include "Editor/Common/Commands/Scene/SiblingOrderKey.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

// EditorSiblingOrderTest — 형제 옮기기가 정하는 키(분수 색인, 설계 E-6). 씬 밖의 지역 매니저로 키 계획만 본다(기록 · 되돌리기는 editor/hierarchyorder 시나리오).

namespace
{
    struct EditorSiblingOrderTestInternal
    {
        /** @brief 목록에서 @p pObject 를 빼 @p position 에 다시 넣는다(끌어 놓기가 짓는 목록). */
        static void moveInList( sw::vector<sw::GameObject*>& inoutList, sw::GameObject* pObject, size_t position )
        {
            for ( size_t index = 0; index < inoutList.size(); ++index )
            {
                if ( inoutList[index] == pObject )
                {
                    inoutList.erase( inoutList.begin() + static_cast<ptrdiff_t>( index ) );
                    break;
                }
            }
            inoutList.insert( inoutList.begin() + static_cast<ptrdiff_t>( position ), pObject );
        }

        static void applyEdits( const sw::vector<sw::editor::EditorSiblingOrder::SiblingOrderEdit>& listEdit )
        {
            for ( const sw::editor::EditorSiblingOrder::SiblingOrderEdit& edit : listEdit )
                edit._pObject->setSiblingOrder( edit._key );
        }
    };
} // namespace

/**
 * @brief [EditorSiblingOrderTest] 틈을 둔 형제 사이로 옮기면 옮긴 오브젝트 하나의 키만 바뀐다 — 되돌리기 기록도 (id, 키) 한 쌍이다
 */
SW_TEST_CASE( EditorSiblingOrderTest, MoveRecordsOneObject )
{
    using Order    = sw::editor::EditorSiblingOrder;
    using Internal = EditorSiblingOrderTestInternal;
    sw::GameObjectManager       manager;
    sw::vector<sw::GameObject*> listSibling;
    for ( const utf8* pName : { "A", "B", "C", "D" } )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        SW_REQUIRE_NOT_NULL( pObject );
        pObject->setSiblingOrder( static_cast<sw::uint32>( listSibling.size() + 1 ) * sw::editor::SiblingOrderKey::kAppendStep );
        listSibling.push_back( pObject );
    }
    sw::GameObject* pMoved = listSibling[3];
    Internal::moveInList( listSibling, pMoved, 1 ); // D 를 A 뒤로

    sw::vector<Order::SiblingOrderEdit> listEdit;
    Order::planKeys( listSibling, 1, listEdit );
    SW_REQUIRE_EQUAL( static_cast<sw::uint32>( listEdit.size() ), 1u );
    SW_EXPECT_TRUE( listEdit[0]._pObject == pMoved );
    Internal::applyEdits( listEdit );

    sw::vector<sw::GameObject*> listSorted = listSibling;
    Order::sortSiblings( listSorted );
    SW_EXPECT_TRUE( listSorted == listSibling ); // 키 순서가 끌어 놓은 순서다

    Order::planKeys( listSibling, 1, listEdit ); // 이미 그 자리면 바꿀 것이 없다
    SW_EXPECT_TRUE( listEdit.empty() );
}

/**
 * @brief [EditorSiblingOrderTest] 키가 모두 같은 옛 씬은 오브젝트 id 순이다 — 옮기면 그 부모의 형제만 다시 펴고 나머지의 상대 순서는 그대로다
 */
SW_TEST_CASE( EditorSiblingOrderTest, TiesKeepObjectIDOrder )
{
    using Order    = sw::editor::EditorSiblingOrder;
    using Internal = EditorSiblingOrderTestInternal;
    sw::GameObjectManager       manager;
    sw::vector<sw::GameObject*> listSibling;
    for ( const utf8* pName : { "A", "B", "C" } )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        SW_REQUIRE_NOT_NULL( pObject );
        listSibling.push_back( pObject ); // 키 0 — 옛 씬
    }
    sw::vector<sw::GameObject*> listSorted = listSibling;
    Order::sortSiblings( listSorted );
    SW_EXPECT_TRUE( listSorted == listSibling ); // 같은 키는 만든 순서(오브젝트 id 순)

    sw::GameObject* pMoved = listSibling[2];
    Internal::moveInList( listSibling, pMoved, 0 ); // C 를 맨 앞으로
    sw::vector<Order::SiblingOrderEdit> listEdit;
    Order::planKeys( listSibling, 0, listEdit );
    SW_EXPECT_EQUAL( static_cast<sw::uint32>( listEdit.size() ), 3u ); // 같은 키 사이에는 틈이 없다 — 다시 편다
    Internal::applyEdits( listEdit );
    listSorted = listSibling;
    Order::sortSiblings( listSorted );
    SW_EXPECT_TRUE( listSorted == listSibling ); // C, A, B
}
