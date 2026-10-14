#include "pch.h"

#include "Editor/Common/Commands/EditorSiblingOrder.h"

#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSiblingOrderInternal
        {
            /** @brief `uint16` 로 저장하는 형제 번호의 상한입니다. 형제가 이보다 많으면 뒤쪽은 같은 번호가 되어 오브젝트 id 순으로 가른다. */
            static constexpr uint32 kMaxSiblingIndex = 65535;

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

            /** @brief 목록의 순서대로 형제 번호를 0 부터 다시 매깁니다. 바뀐 오브젝트마다 되돌리기를 남깁니다(호출하는 쪽의 트랜잭션 안). */
            static void renumber( const vector<GameObject*>& listSibling )
            {
                for ( size_t position = 0; position < listSibling.size(); ++position )
                {
                    GameObject*  pSibling = listSibling[position];
                    const uint32 desired  = position < kMaxSiblingIndex ? static_cast<uint32>( position ) : kMaxSiblingIndex;
                    if ( pSibling->getSiblingIndex() == desired )
                        continue;
                    const ObjectSnapshot before = EditorTransaction::captureSnapshot( pSibling );
                    pSibling->setSiblingIndex( static_cast<uint16>( desired ) );
                    EditorSceneCommands::commitModify( pSibling, before, getUndoLabel() );
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

    SiblingDropZone EditorSiblingOrder::computeDropZone( float32 cursorY, float32 rowMinY, float32 rowMaxY )
    {
        const float32 height = rowMaxY - rowMinY;
        if ( height <= 0.0f )
            return SiblingDropZone::Onto;
        const float32 fraction = ( cursorY - rowMinY ) / height;
        if ( fraction < kEdgeFraction )
            return SiblingDropZone::Before;
        if ( fraction > 1.0f - kEdgeFraction )
            return SiblingDropZone::After;
        return SiblingDropZone::Onto;
    }

    bool EditorSiblingOrder::isBefore( const GameObject* pLeft, const GameObject* pRight )
    {
        if ( pLeft->getSiblingIndex() != pRight->getSiblingIndex() )
            return pLeft->getSiblingIndex() < pRight->getSiblingIndex();
        return pLeft->getObjectID() < pRight->getObjectID();
    }

    void EditorSiblingOrder::sortSiblings( vector<GameObject*>& inoutListSibling )
    {
        std::stable_sort( inoutListSibling.begin(), inoutListSibling.end(), &EditorSiblingOrder::isBefore );
    }

    void EditorSiblingOrder::collectChildrenInOrder( const GameObject& parent, vector<GameObject*>& outListChild )
    {
        parent.getChildren( outListChild );
        sortSiblings( outListChild );
    }

    bool EditorSiblingOrder::moveBeside( GameObjectManager& manager, GameObject* pDragged, GameObject* pTarget, bool bAfter, const GameObject* pSkip )
    {
        using Internal = EditorSiblingOrderInternal;
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
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
            const bool bMoved = pNewParent != nullptr ? EditorSceneCommands::reparent( pDragged, pNewParent, Internal::getUndoLabel() )
                                                      : EditorSceneCommands::unparent( pDragged, Internal::getUndoLabel() );
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
        Internal::renumber( listSibling );
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
        // 묻기만 하므로 태그 표에 올리지 않는다 — `TagID::request` 는 프로세스 태그 표를 키우고, 계층 창이 매 프레임 이 길을 부른다.
        const string_view folderTagName{ kFolderTagName };
        return object.hasTag( TagID{ TagID::computeID( folderTagName.data(), folderTagName.size() ), kFolderTagName }, true );
    }

    GameObject* EditorSiblingOrder::createFolder( GameObjectManager& manager, GameObject* pParent )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return nullptr;
        EditorTransaction::beginTransaction( "Create Folder" );
        GameObject* pFolder = EditorSceneCommands::create( &manager, pParent );
        if ( pFolder == nullptr )
        {
            EditorTransaction::cancelTransaction();
            return nullptr;
        }
        (void)EditorSceneCommands::rename( pFolder, "Folder" ); // 이름이 겹치면 매니저가 번호를 붙인다
        const ObjectSnapshot before = EditorTransaction::captureSnapshot( pFolder );
        pFolder->addTag( TagID::request( kFolderTagName ) );
        EditorSceneCommands::commitModify( pFolder, before, "Create Folder" );
        EditorTransaction::endTransaction();
        return pFolder;
    }
} // namespace sw::editor
