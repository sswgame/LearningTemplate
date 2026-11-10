#include "pch.h"

#include "Editor/Panels/Animation/BoneHierarchyPopup.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_POPUP( BoneHierarchyPopup, 300 );

    BoneHierarchyPopup::BoneHierarchyPopup()
        : IEditorPopup{ false }
        , _viewModel{}
        , _viewState{}
    {
    }

    void BoneHierarchyPopup::drawContent()
    {
        if ( EditorChrome::beginPanel( getPopupTitle(), &_bOpen ) == false )
        {
            EditorChrome::endPanel();
            return;
        }

        _viewModel.gather( _viewState );
        if ( _viewState._content == BoneHierarchyContent::NoSelection )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "BoneNoSelection" ) );
            EditorChrome::endPanel();
            return;
        }

        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "SelectedObject", TextArgumentList().addText( "name", _viewState._selectedName.c_str() ) ).c_str() );
        ImGui::Separator();

        switch ( _viewState._content )
        {
            case BoneHierarchyContent::Tree:
                drawTree();
                break;
            case BoneHierarchyContent::ObjectNotFound:
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "BoneObjectNotFound" ) );
                break;
            case BoneHierarchyContent::NoActiveScene:
            case BoneHierarchyContent::NoSelection:
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoActiveScene" ) );
                break;
        }

        EditorChrome::endPanel();
    }

    void BoneHierarchyPopup::drawTree()
    {
        constexpr uint32 kNoSkip     = UINT32_MAX;
        uint32           pushedDepth = 0;       // 연 오브젝트 노드 수(TreePop 할 수)
        uint32           skipDepth   = kNoSkip; // 접힌 오브젝트의 깊이 — 그보다 깊은 줄은 건너뛴다
        for ( const BoneHierarchyRow& row : _viewState._listRow )
        {
            if ( skipDepth != kNoSkip && row._depth > skipDepth )
                continue;
            skipDepth = kNoSkip;
            while ( pushedDepth > row._depth )
            {
                ImGui::TreePop();
                --pushedDepth;
            }

            void* const pNodeID = reinterpret_cast<void*>( static_cast<uintptr_t>( row._id ) );
            if ( row._bComponent != SW_FALSE )
            {
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                ImGui::TreeNodeEx( pNodeID, flags, "%s", row._name.c_str() );
                continue;
            }
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
            if ( row._bHasChildren == SW_FALSE )
                flags |= ImGuiTreeNodeFlags_Leaf;
            if ( ImGui::TreeNodeEx( pNodeID, flags, "%s", row._name.c_str() ) )
                ++pushedDepth;
            else
                skipDepth = row._depth;
        }
        while ( pushedDepth > 0 )
        {
            ImGui::TreePop();
            --pushedDepth;
        }
    }
} // namespace sw::editor
