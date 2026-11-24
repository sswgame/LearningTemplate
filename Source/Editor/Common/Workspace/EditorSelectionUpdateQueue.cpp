#include "pch.h"

#include "Editor/Common/Workspace/EditorSelectionUpdateQueue.h"

#include "Editor/Common/Workspace/EditorSelection.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    void EditorSelectionUpdateQueue::push( const CommandStack::ObjectEditEvent& event )
    {
        if ( event._kind == ObjectEditKind::Destroyed || event._kind == ObjectEditKind::Recreated )
            _listPending.push_back( event );
    }

    void EditorSelectionUpdateQueue::applyTo( EditorSelection& selection )
    {
        Scene* const             pScene   = selection.getScene();
        GameObjectManager* const pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        for ( const CommandStack::ObjectEditEvent& event : _listPending )
        {
            GameObject* pTarget = pManager != nullptr ? pManager->findGameObjectByID( event._objectID ) : nullptr;
            if ( event._kind == ObjectEditKind::Destroyed )
            {
                if ( pTarget == nullptr )
                    selection.pruneInvalid(); // 그 사이 파괴가 끝났다 — 남은 핸들을 걷는다
                else if ( selection.hasObject( pTarget ) )
                    selection.selectObject( pTarget, SelectionMode::Remove );
            }
            else if ( pTarget != nullptr )
                selection.selectObject( pTarget, SelectionMode::Replace );
        }
        _listPending.clear();
    }
} // namespace sw::editor
