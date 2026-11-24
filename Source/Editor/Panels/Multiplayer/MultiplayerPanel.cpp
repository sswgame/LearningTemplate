/**
 * @file MultiplayerPanel.cpp
 * @brief Multiplayer 창 — 멀티 프로세스 재생을 보는 창 하나입니다. 위쪽의 대상 고르기와, 등록된 탭(`SW_EDITOR_MULTIPLAYER_TAB`)을 그립니다.
 * @details 언리얼 Network Insights · 유니티 Multiplayer Play Mode 창 + Network Simulator 에 해당합니다. 세션은 `EditorContext::getSubsystem<EditorNetPlaySession>()` 하나이고,
 *          툴바의 Net Mode 드롭다운과 이 창이 같은 세션을 봅니다. 창은 세션을 `MultiplayerSessionViewModel` 로만 보고, 프레임 첫머리에 모은 값과 함께 탭에 넘깁니다(MVVM 계약). 탭은 이 폴더의 `Multiplayer<이름>Tab.cpp` 가 한 줄로 더하고(Session · Conditions),
 *          서버 디버깅의 다음 단위와 확장 모듈도 같은 줄을 씁니다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class MultiplayerPanel
     * @brief 대상 고르기와 탭 막대를 그립니다. 세션 갱신(`EditorNetPlaySession::update`)은 이 창이 아니라 툴바가 프레임마다 한다(창을 닫아도 로그가 모인다).
     */
    class MultiplayerPanel final : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "multiplayer";
        /** @brief 처음 열 때의 크기(글자 수)입니다. */
        static constexpr EditorTextExtent kInitialPanelSize = EditorTextExtent::ofText( 101.0f, 29.0f );

        MultiplayerPanel();

        const utf8*      getPanelTitle() const override { return "Multiplayer"; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return kInitialPanelSize; }

    private:
        /** @brief 탭 인스턴스 하나와 그것을 만든 등록입니다. */
        struct TabEntry
        {
            const EditorMultiplayerTabRegistration* _pRegistration{ nullptr };
            unique_ptr<IMultiplayerTab>             _pTab{};
        };

        /** @brief 등록 세대가 바뀌었으면(확장 모듈 로드 · 언로드) 탭 인스턴스를 등록 순서대로 다시 만듭니다. */
        void syncTabs();
        /** @brief 창 위쪽의 대상 고르기(서버 · 클라이언트 N)를 그리고 고른 프로세스 번호를 반환합니다. 세션이 돌지 않으면 -1 입니다. */
        optional<int32> drawTargetPicker();

    private:
        MultiplayerSessionViewModel _viewModel;
        MultiplayerSessionViewState _viewState; ///< 프레임 첫머리에 모은 세션 값(목록 저장 공간을 다시 쓴다)
        vector<TabEntry>            _listTab;
        uint32                      _tabGeneration;      ///< `_listTab` 을 맞춘 등록 세대
        int32                       _targetProcessIndex; ///< 고른 대상(0 = 서버)
    };

    MultiplayerPanel::MultiplayerPanel()
        : EditorPanel( false )
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

    optional<int32> MultiplayerPanel::drawTargetPicker()
    {
        const int32 processCount = static_cast<int32>( _viewState._listProcess.size() );
        if ( processCount == 0 )
            return nullopt; // 안내는 탭마다 제 자리에 있다(Session 의 "No processes" · Conditions · Profiler) — 탭 위에 한 줄 더 두면 같은 말을 두 번 한다
        _targetProcessIndex = MathUtil::clamp( _targetProcessIndex, 0, processCount - 1 );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Target" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        const MultiplayerProcessRow& target = _viewState._listProcess[static_cast<size_t>( _targetProcessIndex )];
        if ( EditorWidgets::beginCombo( "##MultiplayerTarget", target._label.c_str() ) )
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
        gatherViewState( _viewModel, _viewState );
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
                EditorSelfTestMarks::recordFormat( "multiplayer.tab.%#", entry._pRegistration->_pID ); // 자동화가 탭 제목을 눌러 탭을 고른다
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
