#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/Model/AbilityDebugViewModel.h"

#include "Editor/Common/EditorSceneIterationAccess.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw::editor
{
    namespace
    {
        struct AbilityDebugViewModelInternal
        {
            /** @brief 선택한 오브젝트 가운데 컴포넌트를 가진 첫 오브젝트입니다 — 도는 월드(메인 씬)를 디버그하는 창이라 메인 씬의 선택이다. 없으면 nullptr 입니다. */
            [[nodiscard]] static GameObject* findSelectedOwner()
            {
                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                for ( GameObject* pObject : listSelected )
                {
                    if ( pObject != nullptr && pObject->getComponent<AbilitySystemComponent>() != nullptr )
                        return pObject;
                }
                return nullptr;
            }
        };
    } // namespace

    void AbilityDebugViewModel::gather( AbilityDebugViewState& outState )
    {
        outState._targetName.clear();
        outState._bHasSnapshot = SW_FALSE;
        outState._bPlayStopped = EditorPlaySession::getUIState().isStopped() ? SW_TRUE : SW_FALSE;

        // 선택한 오브젝트에 컴포넌트가 있으면 그것을 보고, 없으면 목록에서 고른 것을 이어서 본다.
        if ( GameObject* pSelected = AbilityDebugViewModelInternal::findSelectedOwner() )
            _pickedHandle = pSelected->getHandle();
        GameObjectManager* pObjects     = getActiveObjectManager();
        outState._bHasScene             = pObjects != nullptr ? SW_TRUE : SW_FALSE;
        GameObject*             pOwner  = pObjects != nullptr && _pickedHandle.isValid() ? pObjects->resolveGameObject( _pickedHandle ) : nullptr;
        AbilitySystemComponent* pTarget = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        if ( pTarget == nullptr )
            return;
        outState._targetName = pOwner->getName().c_str();
        if ( outState._bPlayStopped != SW_FALSE )
            return; // 런타임 상태는 플레이 중에만 있다
        AbilityDebugSnapshot::collect( *pTarget, outState._snapshot );
        outState._bHasSnapshot = SW_TRUE;
    }

    void AbilityDebugViewModel::gatherCandidates( vector<AbilityDebugCandidate>& outListCandidate ) const
    {
        outListCandidate.clear();
        GameObjectManager* pObjects = getActiveObjectManager();
        if ( pObjects == nullptr )
            return;
        const GameObject* pCurrent = _pickedHandle.isValid() ? pObjects->resolveGameObject( _pickedHandle ) : nullptr;
        pObjects->forEachGameObject( editor::EditorSceneIterationAccess::getKey(), [&outListCandidate, pCurrent]( GameObject* pObject )
        {
            if ( pObject == nullptr || pObject->getComponent<AbilitySystemComponent>() == nullptr )
                return;
            AbilityDebugCandidate candidate;
            candidate._name     = pObject->getName().c_str();
            candidate._handle   = pObject->getHandle();
            candidate._objectID = static_cast<uint32>( pObject->getObjectID() );
            candidate._bCurrent = pObject == pCurrent ? SW_TRUE : SW_FALSE;
            outListCandidate.push_back( std::move( candidate ) );
        } );
    }
} // namespace sw::editor
