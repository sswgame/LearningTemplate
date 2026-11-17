#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Name/TagID.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/SiblingOrderKey.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSiblingOrderInternal
        {
            static const utf8* getUndoLabel()
            {
                return "Reorder GameObject";
            }

            /** @brief @p pParent 아래 형제를 표시 순서로 모읍니다. 부모가 없으면 씬의 루트입니다. */
            static void collectSiblings( GameObjectManager& manager, const GameObject* pParent, vector<GameObject*>& outListSibling )
            {
                if ( pParent != nullptr )
                    EditorSiblingOrder::collectChildrenInOrder( *pParent, outListSibling );
                else
                    EditorSceneCommands::collectRootsInOrder( manager, outListSibling );
            }

            /** @brief 정한 키를 쓰고 오브젝트마다 (id, 키) 데이터 단계를 남깁니다(호출하는 쪽의 트랜잭션 안 — 한 단계로 묶인다). */
            static void applyKeys( EditorTransaction& transaction, const vector<EditorSiblingOrder::SiblingOrderEdit>& listEdit )
            {
                for ( const EditorSiblingOrder::SiblingOrderEdit& edit : listEdit )
                {
                    const uint32 beforeKey = edit._pObject->getSiblingOrder();
                    edit._pObject->setSiblingOrder( edit._key );
                    transaction.recordSiblingOrder( edit._pObject, beforeKey, edit._key, getUndoLabel() );
                }
            }

            static void removeFromList( vector<GameObject*>& inoutListObject, const GameObject* pObject )
            {
                for ( size_t index = 0; index < inoutListObject.size(); ++index )
                {
                    if ( inoutListObject[index] == pObject )
                    {
                        inoutListObject.erase( inoutListObject.begin() + static_cast<ptrdiff_t>( index ) );
                        return;
                    }
                }
            }
        };
    } // namespace

    bool EditorSiblingOrder::isBefore( const GameObject* pLeft, const GameObject* pRight )
    {
        if ( pLeft->getSiblingOrder() != pRight->getSiblingOrder() )
            return pLeft->getSiblingOrder() < pRight->getSiblingOrder();
        return pLeft->getObjectID() < pRight->getObjectID();
    }

    void EditorSiblingOrder::sortSiblings( vector<GameObject*>& inoutListSibling )
    {
        VectorUtil::stableSort( inoutListSibling, &EditorSiblingOrder::isBefore );
    }

    void EditorSiblingOrder::collectChildrenInOrder( const GameObject& parent, vector<GameObject*>& outListChild )
    {
        parent.getChildren( outListChild );
        sortSiblings( outListChild );
    }

    void EditorSiblingOrder::planKeys( const vector<GameObject*>& listSibling, size_t movedPosition, vector<SiblingOrderEdit>& outListEdit )
    {
        outListEdit.clear();
        if ( movedPosition >= listSibling.size() )
            return;
        GameObject*            pMoved   = listSibling[movedPosition];
        const optional<uint32> previous = movedPosition > 0 ? optional<uint32>{ listSibling[movedPosition - 1]->getSiblingOrder() } : optional<uint32>{ nullopt };
        const optional<uint32> next     = movedPosition + 1 < listSibling.size() ? optional<uint32>{ listSibling[movedPosition + 1]->getSiblingOrder() } : optional<uint32>{ nullopt };
        const uint32           current  = pMoved->getSiblingOrder();
        const bool             bFits    = ( previous.has_value() == false || *previous < current ) && ( next.has_value() == false || current < *next );
        if ( bFits )
            return;
        const optional<uint32> key = SiblingOrderKey::between( previous, next );
        if ( key.has_value() )
        {
            outListEdit.push_back( SiblingOrderEdit{ pMoved, *key } );
            return;
        }
        // 틈이 없다(옛 씬의 같은 키 · 다 찬 틈) — 이 부모의 형제만 고르게 다시 편다.
        vector<uint32> listKey;
        SiblingOrderKey::spread( static_cast<uint32>( listSibling.size() ), listKey );
        for ( size_t position = 0; position < listSibling.size(); ++position )
        {
            if ( listSibling[position]->getSiblingOrder() != listKey[position] )
                outListEdit.push_back( SiblingOrderEdit{ listSibling[position], listKey[position] } );
        }
    }

    bool EditorSiblingOrder::moveBeside( GameObjectManager& manager, GameObject* pDragged, GameObject* pTarget, bool bAfter, const GameObject* pSkip )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        using Internal = EditorSiblingOrderInternal;
        if ( EditorPlaySession::refuseSceneEdit( "Move beside" ) )
            return false;
        const bool bInvalidDragTarget = pDragged == nullptr || pTarget == nullptr || pDragged == pTarget;
        if ( bInvalidDragTarget )
            return false;
        GameObject* pNewParent = pTarget->getParent();
        if ( pNewParent != nullptr && EditorSceneCommands::wouldCreateParentCycle( pDragged, pNewParent ) )
            return false;

        EditorTransaction::beginTransaction( Internal::getUndoLabel() );
        if ( pDragged->getParent() != pNewParent )
        {
            const bool bMoved = pNewParent != nullptr ? EditorSceneCommands::reparent( transaction, pDragged, pNewParent, Internal::getUndoLabel() )
                                                      : EditorSceneCommands::unparent( transaction, pDragged, Internal::getUndoLabel() );
            if ( bMoved == false )
            {
                EditorTransaction::cancelTransaction();
                return false;
            }
        }

        vector<GameObject*> listSibling;
        Internal::collectSiblings( manager, pNewParent, listSibling );
        Internal::removeFromList( listSibling, pDragged );
        if ( pSkip != nullptr )
            Internal::removeFromList( listSibling, pSkip );
        size_t insertPosition = listSibling.size();
        for ( size_t position = 0; position < listSibling.size(); ++position )
        {
            if ( listSibling[position] == pTarget )
            {
                insertPosition = bAfter ? position + 1 : position;
                break;
            }
        }
        listSibling.insert( listSibling.begin() + static_cast<ptrdiff_t>( insertPosition ), pDragged );
        vector<SiblingOrderEdit> listEdit;
        planKeys( listSibling, insertPosition, listEdit );
        Internal::applyKeys( transaction, listEdit );
        EditorTransaction::endTransaction();
        return true;
    }

    bool EditorSiblingOrder::moveStep( GameObjectManager& manager, GameObject* pObject, bool bDown, const GameObject* pSkip )
    {
        if ( pObject == nullptr )
            return false;
        vector<GameObject*> listSibling;
        EditorSiblingOrderInternal::collectSiblings( manager, pObject->getParent(), listSibling );
        if ( pSkip != nullptr )
            EditorSiblingOrderInternal::removeFromList( listSibling, pSkip );
        for ( size_t position = 0; position < listSibling.size(); ++position )
        {
            if ( listSibling[position] != pObject )
                continue;
            if ( bDown ? position + 1 >= listSibling.size() : position == 0 )
                return false;
            GameObject* pNeighbor = listSibling[bDown ? position + 1 : position - 1];
            return moveBeside( manager, pObject, pNeighbor, bDown, pSkip );
        }
        return false;
    }

    bool EditorSiblingOrder::isFolder( const GameObject& object )
    {
        // 묻기만 하므로 태그 테이블에 올리지 않는다 — `TagID::request` 는 프로세스 태그 테이블을 키우고, 계층 창이 매 프레임 이 길을 부른다.
        const string_view folderTagName{ kFolderTagName };
        return object.hasTag( TagID{ TagID::computeID( folderTagName.data(), folderTagName.size() ), kFolderTagName }, true );
    }

    GameObject* EditorSiblingOrder::createFolder( GameObjectManager& manager, GameObject* pParent )
    {
        if ( EditorPlaySession::refuseSceneEdit( "Create folder" ) )
            return nullptr;
        EditorTransaction transaction{ "Create Folder" };
        GameObject*       pFolder = EditorSceneCommands::create( transaction, &manager, pParent );
        if ( pFolder == nullptr )
        {
            EditorTransaction::cancelTransaction(); // 구간 전체를 버린다 — 닫기(소멸자)는 깊이 0 이라 아무것도 하지 않는다
            return nullptr;
        }
        (void)EditorSceneCommands::rename( transaction, pFolder, "Folder" ); // 이름이 겹치면 매니저가 번호를 붙인다
        const ObjectSnapshot before = EditorTransaction::captureSnapshot( pFolder );
        pFolder->addTag( TagID::request( kFolderTagName ) );
        EditorSceneCommands::commitModify( transaction, pFolder, before, "Create Folder" );
        return pFolder;
    }
} // namespace sw::editor
