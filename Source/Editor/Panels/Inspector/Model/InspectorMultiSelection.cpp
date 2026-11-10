#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorMultiSelection.h"

#include "Editor/Common/Commands/Scene/EditorMultiEdit.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw::editor
{
    InspectorMultiSelection::ActiveState InspectorMultiSelection::readActiveState( const vector<GameObject*>& listObject )
    {
        ActiveState state{};
        if ( listObject.empty() )
            return state;
        state._bActive = listObject[0]->isActive();
        for ( const GameObject* pObject : listObject )
        {
            state._bMixed = state._bMixed || pObject->isActive() != state._bActive;
        }
        return state;
    }

    void InspectorMultiSelection::applyActive( const vector<GameObject*>& listObject, bool bActive )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        EditorTransaction::beginTransaction( "Set Active (multiple)" );
        for ( GameObject* pObject : listObject )
        {
            EditorSceneCommands::setActive( transaction, pObject, bActive );
        }
        EditorTransaction::endTransaction();
    }

    size_t InspectorMultiSelection::countUnsharedComponents( const GameObject& primary, const vector<EditorMultiEditComponent>& listCommon )
    {
        size_t unsharedCount{ 0 };
        for ( const Component* pComponent : primary.getComponents() )
        {
            if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                continue;
            bool bShared = false;
            for ( const EditorMultiEditComponent& common : listCommon )
            {
                bShared = bShared || common._pType == pComponent->getTypeInfo();
            }
            unsharedCount += bShared ? 0u : 1u;
        }
        return unsharedCount;
    }

    bool InspectorMultiSelection::hasMixedValues( const PropertyInfo& prop, const vector<Component*>& listComponent )
    {
        vector<ObjectAddress> listInstance;
        listInstance.reserve( listComponent.size() );
        for ( const Component* pComponent : listComponent )
        {
            listInstance.push_back( ObjectAddress{ pComponent } );
        }
        return EditorMultiEditComponent::hasMixedValues( prop, listInstance );
    }
} // namespace sw::editor
