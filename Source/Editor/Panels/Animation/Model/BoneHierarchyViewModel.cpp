#include "pch.h"

#include "Editor/Panels/Animation/Model/BoneHierarchyViewModel.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    void BoneHierarchyViewModel::gather( BoneHierarchyViewState& outState ) const
    {
        outState._listRow.clear();
        outState._selectedName.clear();
        outState._content = BoneHierarchyContent::NoSelection;

        EditorWorkspace& workspace    = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        outState._selectedName        = workspace.getSelectedObjectName();
        const uint64 selectedObjectID = workspace.getSelectedObjectID();
        if ( outState._selectedName.empty() || selectedObjectID == 0 )
            return;

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            outState._content = BoneHierarchyContent::NoActiveScene;
            return;
        }
        const GameObject* pSelectedObj = pScene->getObjectManager()->findGameObjectByID( selectedObjectID );
        if ( pSelectedObj == nullptr )
        {
            outState._content = BoneHierarchyContent::ObjectNotFound;
            return;
        }
        collectObjectRows( *pSelectedObj, 0, outState._listRow );
        outState._content = BoneHierarchyContent::Tree;
    }

    void BoneHierarchyViewModel::collectObjectRows( const GameObject& object, uint32 depth, vector<BoneHierarchyRow>& outListRow )
    {
        const utf8* pObjName = object.getName().c_str();
        if ( StringUtil::isNullOrEmpty( pObjName ) )
            pObjName = "GameObject";

        vector<GameObject*> listChildren;
        object.getChildren( listChildren );
        const SceneComponent* pRootComp = object.getPrimarySceneComponent();

        BoneHierarchyRow row{};
        row._name         = pObjName;
        row._id           = object.getObjectID();
        row._depth        = depth;
        row._bHasChildren = ( listChildren.empty() == false || pRootComp != nullptr ) ? SW_TRUE : SW_FALSE;
        outListRow.push_back( std::move( row ) );

        if ( pRootComp != nullptr )
            collectComponentRows( *pRootComp, depth + 1, outListRow );
        for ( const GameObject* pChild : listChildren )
        {
            if ( pChild != nullptr )
                collectObjectRows( *pChild, depth + 1, outListRow );
        }
    }

    void BoneHierarchyViewModel::collectComponentRows( const SceneComponent& component, uint32 depth, vector<BoneHierarchyRow>& outListRow )
    {
        const utf8*       pCompName = "SceneComponent";
        const GameObject* pOwner    = component.getOwner();
        if ( pOwner != nullptr && StringUtil::isNullOrEmpty( pOwner->getName().c_str() ) == false )
            pCompName = pOwner->getName().c_str();

        BoneHierarchyRow row{};
        row._name       = pCompName;
        row._id         = static_cast<uint64>( component.getComponentID() );
        row._depth      = depth;
        row._bComponent = SW_TRUE;
        outListRow.push_back( std::move( row ) );

        // 컴포넌트 계층은 한 단(잎 줄)으로 편다 — 예전 그리기도 NoTreePushOnOpen 이라 들여쓰지 않았다.
        for ( const SceneComponent* pChild : component.getChildren() )
        {
            if ( pChild != nullptr )
                collectComponentRows( *pChild, depth, outListRow );
        }
    }
} // namespace sw::editor
