#include "pch.h"

#include "Editor/Popups/BoneHierarchyPopup.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct BoneHierarchyPopupInternal
        {
            static void drawSceneComponentHierarchy( const SceneComponent* pComp )
            {
                if ( pComp == nullptr )
                    return;

                const utf8*       pCompName = "SceneComponent";
                const GameObject* pOwner    = pComp->getOwner();
                if ( pOwner != nullptr )
                {
                    const utf8* pNameStr = pOwner->getName().c_str();
                    if ( StringUtil::isNullOrEmpty( pNameStr ) == false )
                        pCompName = pNameStr;
                }

                const ImGuiID            nodeID = static_cast<ImGuiID>( pComp->getComponentID() );
                const ImGuiTreeNodeFlags flags  = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

                ImGui::TreeNodeEx( reinterpret_cast<void*>( static_cast<uintptr_t>( nodeID ) ), flags, "%s", pCompName );

                const vector<SceneComponent*>& listChildren = pComp->getChildren();
                for ( const SceneComponent* pChild : listChildren )
                {
                    if ( pChild != nullptr )
                        drawSceneComponentHierarchy( pChild );
                }
            }

            static void drawGameObjectHierarchy( const GameObject* pObj )
            {
                if ( pObj == nullptr )
                    return;

                const utf8* pObjName = pObj->getName().c_str();
                if ( StringUtil::isNullOrEmpty( pObjName ) )
                    pObjName = "GameObject";

                vector<GameObject*> listChildren;
                pObj->getChildren( listChildren );
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_POPUP( BoneHierarchyPopup, 300 );

    // ------------------------------------------------------------------------------
    // 생성자
    // ------------------------------------------------------------------------------
    BoneHierarchyPopup::BoneHierarchyPopup()
        : IEditorPopup{ false }
    {
    }

    // ------------------------------------------------------------------------------
    // 정적 함수
    // ------------------------------------------------------------------------------
    void BoneHierarchyPopup::open()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        pContext->getSubsystem<EditorPopupManager>().openPopup( kPopupID );
    }

    void BoneHierarchyPopup::close()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        pContext->getSubsystem<EditorPopupManager>().closePopup( kPopupID );
    }

    void BoneHierarchyPopup::toggle()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        pContext->getSubsystem<EditorPopupManager>().togglePopup( kPopupID );
    }

    bool BoneHierarchyPopup::isOpen()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;

        return pContext->getSubsystem<EditorPopupManager>().isPopupOpen( kPopupID );
    }

    // ------------------------------------------------------------------------------
    // 내용 그리기
    // ------------------------------------------------------------------------------
    void BoneHierarchyPopup::drawContent()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        if ( EditorChrome::beginPanel( getPopupTitle(), &_bOpen ) == false )
        {
            EditorChrome::endPanel();
            return;
        }

        EditorWorkspace& ws   = editorContext.getSubsystem<EditorWorkspace>();
        const string     name = ws.getSelectedObjectName();
        if ( name.empty() || ws.getSelectedObjectID() == 0 )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "BoneNoSelection" ) );
            EditorChrome::endPanel();
            return;
        }

        ImGui::Text( "Selected Object: %s", name.c_str() );
        ImGui::Separator();

        Scene* pScene = editor::getActiveScene();
        if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
        {
            GameObject* pSelectedObj = pScene->getObjectManager()->findGameObjectByID( ws.getSelectedObjectID() );
            if ( pSelectedObj != nullptr )
                BoneHierarchyPopupInternal::drawGameObjectHierarchy( pSelectedObj );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "BoneObjectNotFound" ) );
        }
        else
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoActiveScene" ) );

        EditorChrome::endPanel();
    }
} // namespace sw::editor
