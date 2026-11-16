#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerConditionsViewModel.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class MultiplayerConditionsTab
     * @brief Conditions 탭 — 고른 대상 프로세스의 회선 흉내를 실행 중에 바꿉니다(서버 디버깅 S4, 언리얼 Net Emulation · 유니티 Network Simulator 에 해당합니다).
     * @details 프리셋, 올림 · 내림 슬라이더(지연 · 흔들림 · 손실 · 대역폭), 끊기 · 재접속 · 스파이크를 `MultiplayerConditionsViewModel` 의 명령으로 보낸다.
     *          패널 값은 ViewModel 이 프로세스마다 들고, 이 탭은 대상의 값을 모아 그리고 슬라이더를 끄는 동안 돌려준다(MVVM 계약).
     */
    class MultiplayerConditionsTab final : public IMultiplayerTab
    {
    public:
        MultiplayerConditionsTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 한 방향 슬라이더 그룹을 그립니다. 값이 바뀌었으면 true 이고, 손을 뗐을 때 바뀌었으면 @p outReleased 가 true 입니다. */
        static bool drawLinkSliders( const utf8* pID, EditorNetLinkSettings& inoutSettings, bool& outReleased );
        /** @brief 대상의 마지막 보고(`net.emulation stats` 의 답)를 한 줄로 그립니다. */
        static void drawReport( const MultiplayerProcessRow& status );

    private:
        MultiplayerConditionsViewModel _viewModel;
        MultiplayerConditionsViewState _viewState;
        float32                        _spikeSeconds;
        bool                           _bApplyToAll; ///< 대상 대신 모든 프로세스에 보낸다 NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerConditionsTab::MultiplayerConditionsTab()
        : _viewModel{}
        , _viewState{}
        , _spikeSeconds{ EditorNetLinkSettings::kDefaultSpikeSeconds }
        , _bApplyToAll{ false }
    {
    }

    bool MultiplayerConditionsTab::drawLinkSliders( const utf8* pID, EditorNetLinkSettings& inoutSettings, bool& outReleased )
    {
        bool bChanged = false;
        outReleased   = false;
        ImGui::PushID( pID );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "LatencyMs" ) );
        bChanged |= EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Latencyms" ), &inoutSettings._latencyMilliseconds, 0.0f, EditorNetLinkSettings::kMaxLatencyMilliseconds, "%.0f" );
        outReleased |= ImGui::IsItemDeactivatedAfterEdit();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "JitterMs" ) );
        bChanged |= EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Jitterms" ), &inoutSettings._jitterMilliseconds, 0.0f, EditorNetLinkSettings::kMaxJitterMilliseconds, "%.0f" );
        outReleased |= ImGui::IsItemDeactivatedAfterEdit();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Loss" ) );
        bChanged |= EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Loss" ), &inoutSettings._lossPercent, 0.0f, 100.0f, "%.1f" );
        outReleased |= ImGui::IsItemDeactivatedAfterEdit();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "BandwidthKBS" ) );
        bChanged |= EditorWidgets::sliderInt( SW_WIDGET_ID( "##BandwidthKBs" ), &inoutSettings._bandwidthKilobytesPerSecond, 0, EditorNetLinkSettings::kMaxBandwidthKilobytesPerSecond, "%d" );
        outReleased |= ImGui::IsItemDeactivatedAfterEdit();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsBandwidth" ) );
        ImGui::PopID();
        return bChanged;
    }

    void MultiplayerConditionsTab::drawReport( const MultiplayerProcessRow& status )
    {
        if ( status._emulationReportCount == 0 )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ConditionsNoStats" ) );
            return;
        }
        // 보고 줄을 그대로 보인다 — 키 이름이 곧 필드 이름이다(connections · sent · dropped · resent · lossPercent · rttMs).
        ImGui::TextUnformatted( status._emulationReport.makeLine().c_str() );
    }

    void MultiplayerConditionsTab::drawTab( const MultiplayerTabContext& context )
    {
        _viewModel.gather( context._targetProcessIndex, _viewState );
        if ( _viewState._bActive == SW_FALSE )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ConditionsNoSession" ) );
            return;
        }
        const int32                  targetIndex = *context._targetProcessIndex; // `_bActive` 는 대상이 있을 때만 참이다
        const MultiplayerProcessRow& status      = context._pState->_listProcess[static_cast<size_t>( targetIndex )];

        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "AllProcesses" ), &_bApplyToAll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsApplyToAll" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", _bApplyToAll ? "every process" : status._label.c_str() );

        // 프리셋 — 두 방향을 같은 값으로(왕복이 그 회선이 된다). 단추는 엔진 프리셋 테이블의 항목이다.
        if ( _viewState._bPresetTableFailed != SW_FALSE )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NetworkPresetsCouldNotBeReadSeeThe" ) );
        for ( size_t presetIndex = 0; presetIndex < _viewState._listPresetLabel.size(); ++presetIndex )
        {
            const string& label = _viewState._listPresetLabel[presetIndex];
            if ( presetIndex > 0 )
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) );
            if ( ImGui::Button( label.c_str() ) )
                _viewModel.applyPreset( presetIndex, targetIndex, _bApplyToAll );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsPreset" ) );

        ImGui::Separator();
        if ( ImGui::BeginTable( "##ConditionsLinks", 2, ImGuiTableFlags_SizingStretchSame ) )
        {
            EditorNetLinkSettings up        = _viewState._up;
            EditorNetLinkSettings down      = _viewState._down;
            bool                  bUpDone   = false;
            bool                  bDownDone = false;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "UpSend" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsUp" ) );
            bool bChanged = drawLinkSliders( "##Up", up, bUpDone );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "DownReceive" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsDown" ) );
            bChanged |= drawLinkSliders( "##Down", down, bDownDone );
            ImGui::EndTable();
            if ( bChanged )
                _viewModel.setLink( targetIndex, up, down ); // 끄는 동안의 값 — 보내기는 손을 뗐을 때다
            if ( bUpDone )
                _viewModel.sendLink( EditorNetLinkDirection::Up, targetIndex, _bApplyToAll );
            if ( bDownDone )
                _viewModel.sendLink( EditorNetLinkDirection::Down, targetIndex, _bApplyToAll );
        }

        ImGui::Separator();
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Disconnect" ) ) )
            _viewModel.disconnect( targetIndex, _bApplyToAll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsDisconnect" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Reconnect" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Reconnect" ) ) )
            _viewModel.reconnect( targetIndex, _bApplyToAll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsReconnect" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldNumberEm" ) );
        EditorWidgets::inputFloat( SW_WIDGET_ID( "##SpikeSeconds" ), &_spikeSeconds, 0.0f, 0.0f, "%.1f s" );
        _spikeSeconds = MathUtil::max( 0.0f, _spikeSeconds );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Spike" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Spike" ) ) )
            _viewModel.spike( _spikeSeconds, targetIndex, _bApplyToAll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsSpike" ) );

        ImGui::Separator();
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RefreshStats" ) ) )
            _viewModel.refreshStats( targetIndex, _bApplyToAll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsRefreshStats" ) );
        ImGui::SameLine();
        drawReport( status );
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerConditionsTab, "conditions", 200, "Conditions" );
} // namespace sw::editor
