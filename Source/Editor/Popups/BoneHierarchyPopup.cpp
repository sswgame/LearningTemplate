#include "pch.h"

#include "Editor/Popups/BoneHierarchyPopup.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw
{
	namespace
	{
		void drawSceneComponentHierarchy( const SceneComponent* pComp )
		{
			if ( pComp == nullptr )
				return;

			const utf8*		  pCompName = "SceneComponent";
			const GameObject* pOwner	= pComp->getOwner();
			if ( pOwner != nullptr )
			{
				const utf8* pNameStr = pOwner->getName().c_str();
				if ( pNameStr != nullptr && pNameStr[0] != '\0' )
					pCompName = pNameStr;
			}

			const vector<SceneComponent*>& listChildren = pComp->getChildren();
			ImGuiTreeNodeFlags			   flags		= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
			if ( listChildren.empty() )
				flags |= ImGuiTreeNodeFlags_Leaf;

			const bool bOpen = ImGui::TreeNodeEx( pComp, flags, "%s", pCompName );
			if ( bOpen )
			{
				for ( const SceneComponent* pChild : listChildren )
				{
					drawSceneComponentHierarchy( pChild );
				}
				ImGui::TreePop();
			}
		}

		void drawGameObjectHierarchy( const GameObject* pObj )
		{
			if ( pObj == nullptr )
				return;

			const utf8* pObjName = pObj->getName().c_str();
			if ( pObjName == nullptr || pObjName[0] == '\0' )
				pObjName = "GameObject";

			const vector<GameObject*>& listChildren = pObj->getChildren();
			ImGuiTreeNodeFlags		   flags		= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
			if ( listChildren.empty() && pObj->getPrimarySceneComponent() == nullptr )
				flags |= ImGuiTreeNodeFlags_Leaf;

			const bool bOpen = ImGui::TreeNodeEx( pObj, flags, "%s", pObjName );
			if ( bOpen )
			{
				const SceneComponent* pRootComp = pObj->getPrimarySceneComponent();
				if ( pRootComp != nullptr )
					drawSceneComponentHierarchy( pRootComp );

				for ( const GameObject* pChild : listChildren )
				{
					drawGameObjectHierarchy( pChild );
				}
				ImGui::TreePop();
			}
		}
	} // namespace

	void drawBoneHierarchyPopup()
	{
		if ( EditorWorkspace::boneHierarchyPopupOpen() == false )
			return;

		if ( editor::beginPanel( "Hierarchy / Skeleton View", &EditorWorkspace::boneHierarchyPopupOpen() ) == false )
		{
			editor::endPanel();
			return;
		}

		const string name = EditorWorkspace::selectedObjectName();
		if ( name.empty() || EditorWorkspace::selectedObjectId() == 0 )
		{
			editor::drawEmptyHint( "No selection. Select an object in the Hierarchy." );
			editor::endPanel();
			return;
		}

		ImGui::Text( "Selected Object: %s", name.c_str() );
		ImGui::Separator();

		Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
		if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
		{
			GameObject* pSelectedObj = pScene->getObjectManager()->findGameObjectById( EditorWorkspace::selectedObjectId() );
			if ( pSelectedObj != nullptr )
				drawGameObjectHierarchy( pSelectedObj );
			else
				editor::drawEmptyHint( "Object not found in active scene." );
		}
		else
			editor::drawEmptyHint( "No active scene." );

		editor::endPanel();
	}
} // namespace sw
