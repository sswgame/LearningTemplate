#include "pch.h"

#include "Editor/Panels/Multiplayer/MultiplayerPanel.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    MultiplayerPanel::MultiplayerPanel()
        : IEditorPanel( false )
        , _viewModel{}
        , _viewState{}
        , _listTab{}
        , _tabGeneration{ constant::invalid_index::kUint32 }
        , _targetProcessIndex{ MultiplayerSessionViewModel::kServerProcessIndex }
    {
    }

    void MultiplayerPanel::syncTabs()
    {
        using Registry                              = EditorExtensionRegistry<EditorMultiplayerTabRegistration>;
        const EditorExtensionRegistrationList& list = Registry::getList();
        if ( list.getGeneration() == _tabGeneration )
            return;
        _tabGeneration = list.getGeneration();
        _listTab.clear();
        _listTab.reserve( Registry::getCount() );
        for ( uint32 index = 0; index < Registry::getCount(); ++index )
        {
            const EditorMultiplayerTabRegistration& registration = Registry::getAt( index );
            TabEntry&                               entry        = _listTab.emplace_back();
            entry._pRegistration                                 = &registration;
            entry._pTab                                          = registration._pCreate();
        }
    }

    int32 MultiplayerPanel::drawTargetPicker()
    {
        const int32 processCount = static_cast<int32>( _viewState._listProcess.size() );
        if ( processCount == 0 )
            return -1; // 안내는 탭마다 제 자리에 있다(Session 의 "No processes" · Conditions · Profiler) — 탭 위에 한 줄 더 두면 같은 말을 두 번 한다
        _targetProcessIndex = MathUtil::clamp( _targetProcessIndex, 0, processCount - 1 );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Target" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        const MultiplayerProcessRow& target = _viewState._listProcess[static_cast<size_t>( _targetProcessIndex )];
        if ( ImGui::BeginCombo( "##MultiplayerTarget", target._label.c_str() ) )
        {
            for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
            {
                if ( ImGui::Selectable( _viewState._listProcess[static_cast<size_t>( processIndex )]._label.c_str(), processIndex == _targetProcessIndex ) )
                    _targetProcessIndex = processIndex;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "Target" ) );
        return _targetProcessIndex;
    }

    void MultiplayerPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        syncTabs();
        MultiplayerTabContext context;
        context._pSession           = &_viewModel;
        context._pState             = &_viewState;
        context._targetProcessIndex = drawTargetPicker();
        if ( ImGui::BeginTabBar( "##MultiplayerTabs" ) )
        {
            for ( TabEntry& entry : _listTab )
            {
                const bool bTabOpen = ImGui::BeginTabItem( entry._pRegistration->_pLabel );
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::record( ( string( "multiplayer.tab." ) + entry._pRegistration->_pID ).c_str() ); // 자동화가 탭 제목을 눌러 탭을 고른다
                if ( bTabOpen )
                {
                    entry._pTab->drawTab( context );
                    ImGui::EndTabItem();
                }
                else
                {
                    entry._pTab->updateWhileHidden( context );
                }
            }
            ImGui::EndTabBar();
        }
    }

    SW_EDITOR_PANEL( MultiplayerPanel, MultiplayerPanel::kPanelID, EditorPanelCategory::Tool, 2035 );

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorMultiplayerTabRegistration )
} // namespace sw::editor
