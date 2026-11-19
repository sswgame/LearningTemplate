#include "pch.h"

#include "Editor/Panels/Memory/MemoryPanel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/ContentBrowser/ContentBrowserPanel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MemoryPanelInternal
        {
            static constexpr float32 kComboWidth       = 220.0f; ///< 스냅숏 · 비교 콤보 폭(96 DPI 픽셀)
            static constexpr float32 kSearchWidth      = 240.0f; ///< 이름 거르기 폭
            static constexpr float32 kKindFilterWidth  = 180.0f; ///< 형식 거르기 폭
            static constexpr float32 kNameColumnWeight = 3.0f;   ///< 이름 칸(첫 칸)의 폭 몫 — 경로 · 콜스택 글
            static constexpr float32 kValueWeight      = 1.0f;   ///< 나머지 칸의 폭 몫
            static constexpr float32 kBarWeight        = 3.0f;   ///< 태그 막대 칸의 폭 몫
            static constexpr float32 kFullWidth        = -1.0f;  ///< ImGui 의 "남은 폭 전부"
            static constexpr int32   kTableIDStride    = 2;      ///< 시트마다 표 id 두 칸(시트 표 · 비교 표)
            static constexpr int32   kTagBarColumns    = 3;      ///< 태그 막대 표의 칸(이름 · 막대 · 바이트)
            static constexpr size_t  kDiffTailColumns  = 2;      ///< 비교 표 끝의 색 칸(Delta · Status)

            /** @brief 칸 안에서 글을 오른쪽 끝에 맞춥니다(숫자 칸 — 자릿수가 같은 오른쪽 끝에 선다). */
            static void textRightAligned( const string& text )
            {
                const float32 width = ImGui::CalcTextSize( text.c_str() ).x;
                const float32 room  = ImGui::GetContentRegionAvail().x;
                if ( room > width )
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + room - width );
                ImGui::TextUnformatted( text.c_str() );
            }

            /** @brief 비교 상태의 글을 그립니다 — 메모리가 늘면 경고색, 줄면 성공색, 같으면 흐리게(색이 뜻을 말한다: 좋음 · 나쁨). */
            static void drawStatusText( const string& text, MemoryDiffStatus status, bool bRightAligned )
            {
                if ( bRightAligned )
                {
                    const float32 width = ImGui::CalcTextSize( text.c_str() ).x;
                    const float32 room  = ImGui::GetContentRegionAvail().x;
                    if ( room > width )
                        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + room - width );
                }
                switch ( status )
                {
                    case MemoryDiffStatus::New:
                    case MemoryDiffStatus::Grown:
                        EditorThemeUtil::textWarning( text.c_str() );
                        return;
                    case MemoryDiffStatus::Shrunk:
                    case MemoryDiffStatus::Gone:
                        EditorThemeUtil::textSuccess( text.c_str() );
                        return;
                    case MemoryDiffStatus::Same:
                        EditorThemeUtil::textMuted( text.c_str() );
                        return;
                }
            }
        };
    } // namespace

    SW_EDITOR_PANEL( MemoryPanel, MemoryPanel::kPanelID, EditorPanelCategory::Tool, MemoryPanel::kMenuOrder );

    MemoryPanel::MemoryPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
        , _nameFilter{}
        , _selectedAssetPath{}
    {
    }

    void MemoryPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        drawToolbar();
        if ( _viewState._statusLine.empty() == false )
            EditorThemeUtil::textMuted( _viewState._statusLine.c_str() );
        if ( _viewState._currentIndex < 0 )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMemory", "NoSnapshots" ) );
            return;
        }
        drawSummary();
        drawSheetTabs();
        drawFilterRow();
        drawTable();
    }

    void MemoryPanel::drawToolbar()
    {
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Camera" ), SW_LOCKEY( "EditorMemory", "Capture" ) ) ) )
            _viewModel.captureLocal();
        EditorSelfTestMarks::record( "memory.capture" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMemory", "CaptureTip" ) );

        const utf8* pRemote = SW_LOCKEY( "EditorMemory", "CaptureRemote" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pRemote ) );
        ImGui::BeginDisabled( _viewState._bRemoteAvailable == SW_FALSE );
        if ( ImGui::Button( pRemote ) )
            _viewModel.captureRemote();
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "memory.captureRemote" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMemory", "CaptureRemoteTip" ) );

        if ( _viewState._listSnapshotLabel.empty() )
            return;

        // 보는 스냅숏 · 비교 앞 — 범위를 고르는 칸은 왼쪽, 내보내기는 오른쪽(scope 왼쪽 · utility 오른쪽).
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::SetNextItemWidth( MemoryPanelInternal::kComboWidth * dpiScale );
        if ( EditorWidgets::beginCombo( "##memorySnapshot", _viewState._listSnapshotLabel[static_cast<size_t>( _viewState._currentIndex )].c_str() ) )
        {
            for ( int32 index = 0; index < static_cast<int32>( _viewState._listSnapshotLabel.size() ); ++index )
            {
                ImGui::PushID( index );
                if ( ImGui::Selectable( _viewState._listSnapshotLabel[static_cast<size_t>( index )].c_str(), index == _viewState._currentIndex ) )
                    _viewModel.selectSnapshot( index );
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "memory.snapshot" );

        ImGui::SameLine();
        const utf8* pNoComparison = SW_LOCKEY( "EditorMemory", "NoComparison" );
        const utf8* pPreview      = _viewState._compareIndex >= 0 ? _viewState._listSnapshotLabel[static_cast<size_t>( _viewState._compareIndex )].c_str() : pNoComparison;
        ImGui::SetNextItemWidth( MemoryPanelInternal::kComboWidth * dpiScale );
        if ( EditorWidgets::beginCombo( "##memoryCompare", pPreview ) )
        {
            if ( ImGui::Selectable( pNoComparison, _viewState._compareIndex < 0 ) )
                _viewModel.setCompareIndex( kMemoryNoSnapshot );
            for ( int32 index = 0; index < static_cast<int32>( _viewState._listSnapshotLabel.size() ); ++index )
            {
                if ( index == _viewState._currentIndex )
                    continue;
                ImGui::PushID( index );
                if ( ImGui::Selectable( _viewState._listSnapshotLabel[static_cast<size_t>( index )].c_str(), index == _viewState._compareIndex ) )
                    _viewModel.setCompareIndex( index );
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "memory.compareWith" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMemory", "CompareWithTip" ) );

        const utf8* pPrevious = SW_LOCKEY( "EditorMemory", "ComparePrevious" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pPrevious ) );
        ImGui::BeginDisabled( _viewState._currentIndex <= 0 );
        if ( ImGui::Button( pPrevious ) )
            _viewModel.compareWithPrevious();
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "memory.comparePrevious" );

        const utf8* pExportTable    = SW_LOCKEY( "EditorMemory", "ExportTable" );
        const utf8* pExportSnapshot = SW_LOCKEY( "EditorMemory", "ExportSnapshot" );
        ImGui::BeginDisabled( _viewState._bExportPending != SW_FALSE );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pExportTable ) );
        if ( ImGui::Button( pExportTable ) )
            _viewModel.exportTable();
        EditorSelfTestMarks::record( "memory.exportTable" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pExportSnapshot ) );
        if ( ImGui::Button( pExportSnapshot ) )
            _viewModel.exportSnapshot();
        EditorSelfTestMarks::record( "memory.exportSnapshot" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMemory", "ExportTip" ) );
    }

    void MemoryPanel::drawSummary()
    {
        // 합 두 개가 화면의 첫 답이다 — 이 스냅숏의 CPU · GPU 크기. 그 아래 큰 태그부터 막대(데이터라 강조색).
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EditorMemory", "TotalsLine",
                                                 TextArgumentList().addText( "cpu", _viewState._cpuText ).addText( "gpu", _viewState._gpuText ).addText( "source", _viewState._source ) )
                                    .c_str() );
        if ( _viewState._gpuDescription.empty() == false )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( _viewState._gpuDescription.c_str() );
        }
        if ( _viewState._listTagBar.empty() )
            return;
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
        if ( ImGui::BeginTable( "##memoryTagBars", MemoryPanelInternal::kTagBarColumns, kFlags ) == false )
            return;
        ImGui::TableSetupColumn( "Tag", ImGuiTableColumnFlags_WidthStretch, MemoryPanelInternal::kValueWeight );
        ImGui::TableSetupColumn( "Share", ImGuiTableColumnFlags_WidthStretch, MemoryPanelInternal::kBarWeight );
        ImGui::TableSetupColumn( "Live", ImGuiTableColumnFlags_WidthStretch, MemoryPanelInternal::kValueWeight );
        for ( const MemoryTagBar& bar : _viewState._listTagBar )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( bar._name.c_str() );
            ImGui::TableNextColumn();
            ImGui::ProgressBar( bar._fraction, ImVec2( MemoryPanelInternal::kFullWidth, ImGui::GetTextLineHeight() ), "" );
            ImGui::TableNextColumn();
            MemoryPanelInternal::textRightAligned( bar._bytesText );
        }
        ImGui::EndTable();
    }

    void MemoryPanel::drawSheetTabs()
    {
        if ( ImGui::BeginTabBar( "##memorySheets" ) == false )
            return;
        for ( uint32 sheetIndex = 0; sheetIndex < kMemorySheetCount; ++sheetIndex )
        {
            const MemorySheetKind kind  = static_cast<MemorySheetKind>( sheetIndex );
            const utf8*           pName = getMemorySheetSchema( kind )._pName;
            if ( ImGui::BeginTabItem( pName ) )
            {
                if ( _viewState._sheet != kind )
                {
                    _viewModel.selectSheet( kind );
                    _selectedAssetPath.clear();
                }
                ImGui::EndTabItem();
            }
            EditorSelfTestMarks::recordFormat( "memory.tab.%#", pName );
        }
        ImGui::EndTabBar();
    }

    void MemoryPanel::drawFilterRow()
    {
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        if ( EditorWidgets::drawSearchField( "##memoryNameFilter", _nameFilter, SW_LOCKEY( "EditorMemory", "FilterHint" ), MemoryPanelInternal::kSearchWidth * dpiScale, true, "memory.nameFilter" ) )
            _viewModel.setNameFilter( _nameFilter.c_str() );
        if ( _viewState._listFilterOption.empty() == false )
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth( MemoryPanelInternal::kKindFilterWidth * dpiScale );
            if ( EditorWidgets::beginCombo( "##memoryKindFilter", _viewState._listFilterOption[_viewState._filterIndex].c_str() ) )
            {
                for ( uint32 index = 0; index < static_cast<uint32>( _viewState._listFilterOption.size() ); ++index )
                {
                    ImGui::PushID( static_cast<int32>( index ) );
                    if ( ImGui::Selectable( _viewState._listFilterOption[index].c_str(), index == _viewState._filterIndex ) )
                        _viewModel.setFilterIndex( index );
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::record( "memory.kindFilter" );
        }
        if ( _viewState._sheet == MemorySheetKind::Assets )
        {
            const utf8* pReveal = SW_LOCKEY( "EditorMemory", "ShowInContentBrowser" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pReveal ) );
            ImGui::BeginDisabled( _selectedAssetPath.empty() );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "FolderOpen" ), pReveal ) ) )
                revealSelectedAsset();
            ImGui::EndDisabled();
            EditorSelfTestMarks::record( "memory.reveal" );
        }
        ImGui::SameLine();
        ImGui::TextDisabled( "%u / %u", static_cast<uint32>( _viewState._listRow.size() ), _viewState._sheetRowCount );
    }

    void MemoryPanel::drawTable()
    {
        const vector<MemoryTableColumn>& listColumn = _viewState._listColumn;
        if ( listColumn.empty() )
            return;
        if ( _viewState._listRow.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMemory", "NoRows" ) );
            return;
        }
        // 줄무늬 · 머리 띠 없이 칸 정렬로 읽는다. 시트 · 비교마다 칸이 달라 표 id 를 가른다(정렬 상태가 섞이지 않게).
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
        ImGui::PushID( static_cast<int32>( _viewState._sheet ) * MemoryPanelInternal::kTableIDStride + ( _viewState._bCompareMode != SW_FALSE ? 1 : 0 ) );
        if ( ImGui::BeginTable( "##memoryTable", static_cast<int32>( listColumn.size() ), kFlags, ImGui::GetContentRegionAvail() ) == false )
        {
            ImGui::PopID();
            return;
        }
        ImGui::TableSetupScrollFreeze( 0, 1 );
        for ( uint32 column = 0; column < static_cast<uint32>( listColumn.size() ); ++column )
        {
            ImGuiTableColumnFlags flags = listColumn[column]._bNumeric != SW_FALSE ? ImGuiTableColumnFlags_PreferSortDescending : ImGuiTableColumnFlags_None;
            if ( column == _viewState._sortColumn )
                flags |= ImGuiTableColumnFlags_DefaultSort;
            // 이름 칸(첫 칸)이 가장 넓다 — 경로 · 콜스택 글이 잘리지 않게.
            ImGui::TableSetupColumn( listColumn[column]._name.c_str(), flags, column == 0 ? MemoryPanelInternal::kNameColumnWeight : MemoryPanelInternal::kValueWeight, static_cast<ImGuiID>( column ) );
        }
        ImGui::TableHeadersRow();
        if ( ImGuiTableSortSpecs* pSortSpecs = ImGui::TableGetSortSpecs(); pSortSpecs != nullptr && pSortSpecs->SpecsDirty && pSortSpecs->SpecsCount > 0 )
        {
            _viewModel.setSort( static_cast<uint32>( pSortSpecs->Specs[0].ColumnUserID ), pSortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending );
            pSortSpecs->SpecsDirty = false;
        }

        const bool       bCompareMode = _viewState._bCompareMode != SW_FALSE;
        const size_t     columnCount  = listColumn.size();
        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int32>( _viewState._listRow.size() ) );
        while ( clipper.Step() )
        {
            for ( int32 rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex )
            {
                const MemoryTableRow& row = _viewState._listRow[static_cast<size_t>( rowIndex )];
                ImGui::PushID( rowIndex );
                ImGui::TableNextRow();
                for ( size_t column = 0; column < columnCount && column < row._listCell.size(); ++column )
                {
                    ImGui::TableNextColumn();
                    const bool bNumeric = listColumn[column]._bNumeric != SW_FALSE;
                    if ( column == 0 )
                    {
                        // 줄 고르기 — 첫 칸의 고르기 칸이 줄 전체에 걸친다(고른 줄만 강조색).
                        const bool bSelected = row._assetPath.empty() == false && row._assetPath == _selectedAssetPath;
                        if ( ImGui::Selectable( row._listCell[column].c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns ) && row._assetPath.empty() == false )
                            _selectedAssetPath = row._assetPath;
                        continue;
                    }
                    const bool bDiffValue = bCompareMode && column + MemoryPanelInternal::kDiffTailColumns >= columnCount; // Delta · Status 칸
                    if ( bDiffValue )
                        MemoryPanelInternal::drawStatusText( row._listCell[column], row._status, bNumeric );
                    else if ( bNumeric )
                        MemoryPanelInternal::textRightAligned( row._listCell[column] );
                    else
                        ImGui::TextUnformatted( row._listCell[column].c_str() );
                }
                ImGui::PopID();
            }
        }
        if ( _viewState._listRow.empty() == false )
            EditorSelfTestMarks::record( "memory.table" );
        ImGui::EndTable();
        ImGui::PopID();
    }

    void MemoryPanel::revealSelectedAsset()
    {
        EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
        if ( pPanels == nullptr || pPanels->setPanelOpen<ContentBrowserPanel>( true ) == false )
            return;
        ContentBrowserPanel* pBrowser = pPanels->findPanel<ContentBrowserPanel>();
        if ( pBrowser != nullptr && pBrowser->revealAsset( _selectedAssetPath ) == false )
            SW_LOG_WARNING_ID( "MemoryPanel.AssetNotOnDisk", "'%#' has no loose file to show (it lives only in a cooked pack or is not a path)", _selectedAssetPath );
    }
} // namespace sw::editor
