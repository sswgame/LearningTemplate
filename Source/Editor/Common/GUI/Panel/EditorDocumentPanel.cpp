// 문서 패널의 그리기(열기 막대 · 문서 바꾸기 확인)입니다. 상태 · 읽기 · 되돌리기는 EditorDocumentPanelCore.cpp 에 있다(ImGui 없이 테스트하려고 나눴다).
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    void EditorDocumentPanel::updateFocusedDocument()
    {
        if ( _bConfirmSwitch == SW_TRUE )
        {
            drawUnsavedDocumentPopup();
            return;
        }

        if ( hasNewFocusedDocument() == false )
            return;

        if ( isDocumentDirty() )
        {
            _pendingFocusPath = string{ getMatchingFocusedPath() };
            _bConfirmSwitch   = SW_TRUE;
            drawUnsavedDocumentPopup();
            return;
        }

        acceptFocusedDocument();
    }

    void EditorDocumentPanel::drawDocumentOpenBar( const utf8* pMarkPrefix )
    {
        const string markOpen = string{ pMarkPrefix } + ".open";
        if ( ImGui::Button( "Open..." ) )
        {
            // 열 때 한 번 모은다 — 리소스 트리를 훑으므로 그리기마다 하지 않는다.
            vector<EditorAssetIndexEntry> listEntry;
            EditorAssetIndex::collectEntries( listEntry );
            _listOpenCandidate.clear();
            for ( const EditorAssetIndexEntry& entry : listEntry )
            {
                if ( entry._kind == _kind && acceptsDocumentPath( entry._path ) )
                    _listOpenCandidate.push_back( entry._path );
            }
            std::sort( _listOpenCandidate.begin(), _listOpenCandidate.end() );
            _openFilter.clear();
            EditorInputGUI::openPopup( "##DocumentOpen" );
        }
        EditorSelfTestMarks::note( markOpen.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "DocumentOpen" ) );
        drawOpenDocumentPopup( pMarkPrefix );

        ImGui::SameLine();
        const utf8* pPreview = _loadedAssetPath.empty() ? SW_LOCKEY( "EdCommon", "NoDocument" ) : _loadedAssetPath.c_str();
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::BeginCombo( "##RecentDocuments", pPreview ) )
        {
            if ( _listRecentPath.empty() )
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoRecentDocuments" ) );
            string openPath;
            for ( const string& path : _listRecentPath )
            {
                if ( ImGui::Selectable( path.c_str(), path == _loadedAssetPath ) )
                    openPath = path;
            }
            ImGui::EndCombo();
            if ( openPath.empty() == false )
                requestOpenDocument( openPath );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "RecentDocuments" ) );
    }

    void EditorDocumentPanel::drawOpenDocumentPopup( const utf8* pMarkPrefix )
    {
        if ( ImGui::BeginPopup( "##DocumentOpen" ) == false )
            return;
        const string markPrefix = string{ pMarkPrefix } + ".open.";
        if ( ImGui::IsWindowAppearing() )
            ImGui::SetKeyboardFocusHere();
        EditorWidgets::drawSearchField( "##DocumentOpenSearch", _openFilter, SW_LOCKEY( "EdCommon", "SearchAssetsHint" ), 360.0f * EditorThemeUtil::getDPIScale(), false );
        EditorSelfTestMarks::note( ( markPrefix + "search" ).c_str() );

        const EditorListFilter filter{ _openFilter.c_str() };
        string                 openPath;
        uint32                 shownCount{ 0 };
        for ( const string& path : _listOpenCandidate )
        {
            if ( filter.matchesAny( { string_view{ path } } ) == false )
                continue;
            ++shownCount;
            if ( ImGui::Selectable( path.c_str(), path == _loadedAssetPath ) )
                openPath = path;
            EditorSelfTestMarks::note( ( markPrefix + FileUtil::getFileNamePart( path ) ).c_str() );
        }
        if ( shownCount == 0 )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoAssetsOfKind" ) );
        }
        if ( openPath.empty() == false )
        {
            requestOpenDocument( openPath );
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void EditorDocumentPanel::requestOpenDocument( string_view assetPath )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ assetPath }.c_str() );
    }

    void EditorDocumentPanel::drawUnsavedDocumentPopup()
    {
        if ( ImGui::IsPopupOpen( "##UnsavedDocumentSwitch" ) == false )
            EditorInputGUI::openPopup( "##UnsavedDocumentSwitch" );
        const EditorUnsavedChoice choice =
            EditorWidgets::drawUnsavedChangesModal( "##UnsavedDocumentSwitch",
                                                    "This document has unsaved changes. Switch anyway?" );
        if ( choice == EditorUnsavedChoice::None )
            return;

        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
        {
            // 저장 순서는 기반 클래스가 정한다. 성공했을 때만 dirty 가 지워진다.
            if ( saveDocumentAndClearDirty() == false )
            {
                _bConfirmSwitch = SW_FALSE;
                _pendingFocusPath.clear();
                return;
            }
        }
        if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
            clearDocumentDirty();

        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
        {
            acceptFocusedDocument();
            return;
        }

        _bConfirmSwitch = SW_FALSE;
        _pendingFocusPath.clear();
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( _loadedAssetPath.empty() == false )
            editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( _loadedAssetPath.c_str() );
    }
} // namespace sw::editor
