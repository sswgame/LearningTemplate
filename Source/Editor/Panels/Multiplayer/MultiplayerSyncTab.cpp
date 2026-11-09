#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSyncViewModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MultiplayerSyncTabInternal
        {
            /** @brief 슬롯 이름입니다(탭 · 명령 답). */
            static constexpr const utf8* kArrSourceName[MultiplayerSyncViewModel::kSourceCount] = { "A", "B" };
        };
    } // namespace

    // 클라이언트 답 시한 환경설정 — 값은 `MultiplayerSyncViewModel` 이 읽는다.
    SW_EDITOR_SETTINGS( EditorSyncPreferences, "multiplayer_sync", "Editor/Multiplayer Sync", 370, nullptr );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerSyncTab
     * @brief Sync 탭 — 틱별 상태 해시 이력 두 벌(A · B)을 모아 처음 어긋난 틱을 보입니다(서버 디버깅 S7).
     * @details 원천은 세션이 띄운 전용 서버(디버그 채널의 `state.hashes`), 세션의 클라이언트(표준 입력의 `state.hashline` — 서버가 알린 틱 번호로 남긴 이력),
     *          주소 · 토큰으로 붙은 다른 서버, 저장한 이력 파일이다. 값은 `MultiplayerSyncViewModel::gather` 가 모으고 읽기 · 비교는 그 명령이다(MVVM 계약).
     *          읽기는 누를 때만 한다(채널 요청은 답을 기다린다).
     */
    class MultiplayerSyncTab final : public IMultiplayerTab
    {
    public:
        MultiplayerSyncTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 슬롯 하나의 원천 고르기 · 저장 줄입니다. */
        void drawSource( int32 sourceIndex );
        /** @brief 칸 하나의 클라이언트 원천 단추들입니다. 누르면 그 프로세스의 표준 입력에 `state.hashline` 을 묻고, 답이 오면(`poll`) 칸에 담는다. */
        void drawClientSources( int32 sourceIndex );
        /**
         * @brief 되감기 줄(서버 디버깅 S7 · O7)입니다. 세션이 없으면 `.swintent` 로 멈춘 재생 서버를 띄우고, 재생 서버가 떠 있으면 `step N` 으로 한 틱씩 진행합니다.
         * @details 어긋난 틱을 안 뒤 새 서버를 틱 0 에서 멈춘 채 띄워(`startReplayServer`) 그 틱까지 정확히 진행하면, 같은 틱에서 Which objects differ · Inspector 로 볼 수 있다.
         */
        void drawReplay();

    private:
        MultiplayerSyncViewModel                   _viewModel;
        MultiplayerSyncViewState                   _viewState;
        fixed_string<constant::buffer::kMax64>     _arrChannelName;                                     ///< 읽을 해시 줄 이름(편집 필드)
        fixed_string<constant::file::kMaxPathSize> _arrPath[MultiplayerSyncViewModel::kSourceCount];    ///< 슬롯마다 파일 경로(편집 필드)
        fixed_string<constant::buffer::kMax64>     _arrAddress[MultiplayerSyncViewModel::kSourceCount]; ///< 슬롯마다 채널 주소(편집 필드)
        fixed_string<constant::buffer::kMax128>    _arrToken[MultiplayerSyncViewModel::kSourceCount];   ///< 슬롯마다 채널 토큰(편집 필드)
        fixed_string<constant::file::kMaxPathSize> _arrReplayPath;                                      ///< 재생할 `.swintent` 경로(편집 필드)
        int32                                      _replayStepCount;                                    ///< 재생 서버를 한 번에 진행할 틱 수(편집 필드)
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSyncTab::MultiplayerSyncTab()
        : _viewModel{}
        , _viewState{}
        , _arrChannelName{}
        , _arrPath{}
        , _arrAddress{}
        , _arrToken{}
        , _arrReplayPath{}
        , _replayStepCount{ 1 }
    {
    }

    void MultiplayerSyncTab::drawClientSources( int32 sourceIndex )
    {
        if ( _viewState._bSessionRunning == SW_FALSE )
            return;
        for ( const MultiplayerSyncClientRow& client : _viewState._listClient )
        {
            ImGui::PushID( client._processIndex );
            if ( client._processIndex > 1 )
                ImGui::SameLine();
            const bool bWaiting = client._arrWaiting[sourceIndex] != SW_FALSE;
            ImGui::BeginDisabled( client._bRunning == SW_FALSE || bWaiting );
            if ( ImGui::Button( client._label.c_str() ) )
                _viewModel.requestClientHistory( sourceIndex, client._processIndex );
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncClient" ) );
            ImGui::PopID();
        }
    }

    void MultiplayerSyncTab::drawSource( int32 sourceIndex )
    {
        const MultiplayerSyncSourceRow& source = _viewState._arrSource[sourceIndex];
        ImGui::PushID( sourceIndex );
        ImGui::SeparatorText( MultiplayerSyncTabInternal::kArrSourceName[sourceIndex] );
        if ( source._sampleCount == 0 )
            ImGui::TextDisabled( "empty" );
        else
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "TickS", TextArgumentList().addText( "label", source._label.c_str() ).addInteger( "sampleCount", static_cast<int64>( source._sampleCount ) ).addText( "tick", to_string( source._firstTick ).c_str() ).addText( "tick2", to_string( source._lastTick ).c_str() ) ).c_str() );

        if ( ImGui::Button( SW_LOCKEY( "EdText", "SessionServer" ) ) )
            _viewModel.loadSessionServer( sourceIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncSessionServer" ) );
        ImGui::SameLine();
        drawClientSources( sourceIndex );

        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        ImGui::InputTextWithHint( "##SyncAddress", "127.0.0.1:<port>", _arrAddress[sourceIndex].data(), _arrAddress[sourceIndex].capacity() );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        ImGui::InputTextWithHint( "##SyncToken", "token", _arrToken[sourceIndex].data(), _arrToken[sourceIndex].capacity(), ImGuiInputTextFlags_Password );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ReadServer" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "ReadServer" ) ) )
            _viewModel.readServer( sourceIndex, _arrAddress[sourceIndex].c_str(), _arrToken[sourceIndex].c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncReadServer" ) );

        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldPathEm" ) );
        ImGui::InputTextWithHint( "##SyncPath", "StateHashes/<name>.txt", _arrPath[sourceIndex].data(), _arrPath[sourceIndex].capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "LoadFile" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "LoadFile" ) ) )
            _viewModel.loadFile( sourceIndex, _arrPath[sourceIndex].c_str() ); // 상대 경로는 이 실행의 산출물 폴더 아래다
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        ImGui::BeginDisabled( source._sampleCount == 0 );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            _viewModel.saveFile( sourceIndex, _arrPath[sourceIndex].c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Clear" ) ) )
            _viewModel.clearSource( sourceIndex );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncFile" ) );
        ImGui::PopID();
    }

    void MultiplayerSyncTab::drawReplay()
    {
        const bool bSessionRunning = _viewState._bSessionRunning != SW_FALSE;
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldPathEm" ) );
        ImGui::InputTextWithHint( "##SyncReplayPath", "<name>.swintent", _arrReplayPath.data(), _arrReplayPath.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ReplayServer" ) ) );
        ImGui::BeginDisabled( bSessionRunning || _arrReplayPath.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "ReplayServer" ) ) )
            _viewModel.startReplayServer( _arrReplayPath.c_str() );
        ImGui::EndDisabled();
        // 한 틱씩 진행은 떠 있는 서버가 있을 때만 — 전용 서버는 멈춘 채 떠서 `step` 만큼만 돈다.
        ImGui::BeginDisabled( bSessionRunning == false );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldStepEm" ) );
        ImGui::InputInt( "##SyncReplayStep", &_replayStepCount, 1, 10 );
        _replayStepCount = _replayStepCount < 1 ? 1 : _replayStepCount;
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Step" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Step" ) ) )
            _viewModel.stepServer( _replayStepCount );
        ImGui::EndDisabled();
    }

    void MultiplayerSyncTab::drawTab( const MultiplayerTabContext& /*context*/ )
    {
        _viewModel.poll();
        _viewModel.gather( _viewState );
        // 어느 줄의 이력을 읽을지 — 씬 줄(`scene`) 말고 키트가 더한 줄(`destruction` · `rollback`)도 같은 비교를 탄다. 읽는 단추를 누를 때 이 이름을 쓴다.
        if ( _arrChannelName.empty() )
            _arrChannelName = _viewState._hashChannel.c_str();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        if ( ImGui::InputTextWithHint( "##SyncChannel", "scene | destruction | rollback", _arrChannelName.data(), _arrChannelName.capacity() ) )
            _viewModel.setHashChannel( _arrChannelName.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncChannel" ) );
        for ( int32 sourceIndex = 0; sourceIndex < MultiplayerSyncViewModel::kSourceCount; ++sourceIndex )
        {
            drawSource( sourceIndex );
        }
        ImGui::Separator();
        drawReplay();
        if ( _viewState._error.empty() == false )
            EditorThemeUtil::textError( _viewState._error.c_str() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Compare" ) ) )
            _viewModel.compare();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncCompare" ) );
        if ( _viewState._comparisonText.empty() == false )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( _viewState._comparisonText.c_str() );
        }
        // 어긋난 틱 근처에서 두 서버를 pause 한 채 누른다 — 두 서버의 현재 오브젝트 해시를 다시 받아 다른 오브젝트를 짚는다.
        if ( ImGui::Button( SW_LOCKEY( "EdText", "WhichObjectsDiffer" ) ) )
            _viewModel.startObjectCompare();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncObjects" ) );
        if ( _viewState._bObjectComparison != SW_FALSE )
        {
            fixed_string<constant::buffer::kMax64> text;
            formatstring( text.data(), text.capacity(), "tick %#", _viewState._objectTick );
            ImGui::TextUnformatted( text.c_str() );
            if ( _viewState._listDifferingObject.empty() )
                ImGui::TextUnformatted( "every named object matches" );
            for ( const string& name : _viewState._listDifferingObject )
            {
                ImGui::BulletText( "%s", name.c_str() );
            }
        }
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerSyncTab, "sync", 400, "Sync" );
} // namespace sw::editor
