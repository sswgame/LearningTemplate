#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorStateHashCompare.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Scene/SceneStateHash.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MultiplayerSync" );

    namespace
    {
        struct MultiplayerSyncTabInternal
        {
            /** @brief 주소 · 토큰 칸의 너비(글자 크기 배수)입니다. */
            static constexpr float32 kFieldWidthInFonts = 10.0f;
            /** @brief 경로 칸의 너비(글자 크기 배수)입니다. */
            static constexpr float32 kPathWidthInFonts = 20.0f;
            /** @brief 클라이언트에게 묻는 최근 틱 수입니다. 답은 로그 한 줄(틱마다 약 22 바이트)이라 세션 서버 이력(3600 틱)보다 짧게 묻는다. */
            static constexpr int32 kClientHistoryTicks = 512;
            /** @brief 슬롯 이름입니다(탭 · 명령 답). */
            static constexpr const utf8* kArrSourceName[EditorStateHashCompare::kSourceCount] = { "A", "B" };

            /** @brief 탭과 `netplay.sync` 가 같이 쓰는 비교입니다(창을 닫았다 열어도 남는다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorStateHashCompare& getCompare() { return getEditorState<EditorStateHashCompare>(); }

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
            }

            /** @brief 세션이 띄운 전용 서버의 이력을 칸에 담습니다. 채널이 없으면 false 이고 이유는 `getLastError` 가 아니라 @p outReason 에 씁니다. */
            [[nodiscard]] static bool loadSessionServer( int32 sourceIndex, string& outReason )
            {
                EditorNetPlaySession* pSession   = findSession();
                const bool            bHasServer = pSession != nullptr && pSession->isRunning() && pSession->getDebugChannelPort() != 0;
                if ( bHasServer == false )
                {
                    outReason = "no dedicated server session with a debug channel is running";
                    return false;
                }
                EditorStateHashCompare& compare = getCompare();
                if ( compare.loadFromChannel( sourceIndex, NetAddress::makeLoopback( pSession->getDebugChannelPort() ), pSession->getDebugChannelToken() ) == false )
                {
                    outReason = compare.getLastError();
                    return false;
                }
                return true;
            }

            /** @brief 클라이언트(프로세스 @p processIndex)의 마지막 이력 답을 칸에 담습니다. 답이 오류 형식이면 이력 답 수가 늘지 않으므로 여기 오지 않는다. */
            [[nodiscard]] static bool loadClientReport( int32 sourceIndex, const EditorNetPlayProcessStatus& status )
            {
                string text;
                StateHashHistory::writeReport( status._listStateHash, text );
                return getCompare().loadFromReport( sourceIndex, status._label, text );
            }

            /** @brief 비교 결과 한 줄입니다(탭 · 명령 답). */
            static string describeComparison( const EditorStateHashComparison& comparison )
            {
                if ( comparison._commonTickCount == 0 )
                    return "the two histories share no tick - record them over the same ticks";
                if ( comparison._bDiverged )
                    return "first divergent tick " + to_string( comparison._firstDivergentTick ) + " (" + to_string( comparison._commonTickCount ) + " common tick(s))";
                return "no divergence over " + to_string( comparison._commonTickCount ) + " common tick(s)";
            }

            /** @brief 탐침 `Editor.NetPlaySyncCommonTicks` — 마지막 비교에서 두 이력에 모두 있던 틱 수입니다. */
            [[nodiscard]] static bool readCommonTicks( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getCompare().getComparison()._commonTickCount );
                return true;
            }

            /** @brief 탐침 `Editor.NetPlaySyncDiverged` — 마지막 비교에서 어긋난 틱을 찾았으면 1 입니다. */
            [[nodiscard]] static bool readDiverged( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = getCompare().getComparison()._bDiverged ? 1.0 : 0.0;
                return true;
            }

            /**
             * @brief `netplay.sync save <경로>` — 세션 전용 서버의 이력을 A 에 담아 파일로 씁니다. `netplay.sync compare <경로>` — 세션 서버를 A, 파일을 B 로 담아 비교합니다.
             * @details 탭의 "Session server" · "Load file" · "Save" · "Compare" 와 같은 길이다. 시나리오는 이 명령과 탐침 `Editor.NetPlaySync*` 로 확인한다.
             */
            static bool runSync( const vector<string>& listArgument, string& outReply )
            {
                const bool bSave    = listArgument.size() == 2 && listArgument[0] == "save";
                const bool bCompare = listArgument.size() == 2 && listArgument[0] == "compare";
                if ( bSave == false && bCompare == false )
                    return false;
                EditorStateHashCompare& compare = getCompare();
                if ( loadSessionServer( 0, outReply ) == false )
                    return true;
                if ( bSave )
                {
                    const bool bSaved = compare.saveToFile( 0, listArgument[1] );
                    outReply          = bSaved ? "netplay sync saved " + to_string( compare.getSource( 0 )._listSample.size() ) + " tick(s) to " + listArgument[1]
                                               : "netplay sync could not write " + listArgument[1];
                    return true;
                }
                if ( compare.loadFromFile( 1, listArgument[1] ) == false || compare.compare() == false )
                {
                    outReply = compare.getLastError();
                    return true;
                }
                outReply = "netplay sync " + describeComparison( compare.getComparison() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( editorNetPlaySyncCommonTicks, "Editor.NetPlaySyncCommonTicks", "Ticks both state hash histories of the multiplayer Sync tab held at the last comparison",
                         &MultiplayerSyncTabInternal::readCommonTicks );
    SW_AUTOMATION_PROBE( editorNetPlaySyncDiverged, "Editor.NetPlaySyncDiverged", "1 when the multiplayer Sync tab's last comparison found a divergent tick",
                         &MultiplayerSyncTabInternal::readDiverged );
    SW_DEV_COMMAND( NetPlaySync, "netplay.sync", "netplay.sync save|compare <path>",
                    "Save the session server's per-tick state hash history to a file, or compare it with a saved one (first divergent tick)",
                    &MultiplayerSyncTabInternal::runSync );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerSyncTab
     * @brief Sync 탭 — 틱별 상태 해시 이력 두 벌(A · B)을 모아 처음 어긋난 틱을 보입니다(서버 디버깅 S7).
     * @details 원천은 세션이 띄운 전용 서버(디버그 채널의 `state.hashes`), 세션의 클라이언트(표준 입력의 `state.hashline` — 서버가 알린 틱 번호로 남긴 이력),
     *          주소 · 토큰으로 붙은 다른 서버, 저장한 이력 파일이다. 계산은 `EditorStateHashCompare`.
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
        /** @brief 칸 하나의 클라이언트 원천 단추들입니다. 누르면 그 프로세스의 표준 입력에 `state.hashline` 을 묻고, 답이 오면(`pollClientReplies`) 칸에 담는다. */
        void drawClientSources( int32 sourceIndex );
        /** @brief 기다리는 클라이언트 답이 왔으면 칸에 담습니다. 프레임마다 부릅니다. */
        void pollClientReplies();

        fixed_string<constant::kMaxPathSize>  _arrPath[EditorStateHashCompare::kSourceCount];               ///< 슬롯마다 파일 경로(편집 필드)
        fixed_string<constant::kMaxBuffer64>  _arrAddress[EditorStateHashCompare::kSourceCount];            ///< 슬롯마다 채널 주소(편집 필드)
        fixed_string<constant::kMaxBuffer128> _arrToken[EditorStateHashCompare::kSourceCount];              ///< 슬롯마다 채널 토큰(편집 필드)
        int32                                 _arrPendingProcess[EditorStateHashCompare::kSourceCount];     ///< 슬롯마다 답을 기다리는 클라이언트 프로세스 번호(-1 이면 없다)
        uint32                                _arrPendingReportCount[EditorStateHashCompare::kSourceCount]; ///< 물을 때의 그 프로세스 이력 답 수 — 늘면 새 답이다
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSyncTab::MultiplayerSyncTab()
        : _arrPath{}
        , _arrAddress{}
        , _arrToken{}
        , _arrPendingProcess{ -1, -1 }
        , _arrPendingReportCount{}
    {
        static_assert( EditorStateHashCompare::kSourceCount == 2, "the pending client table is initialized for two sources" );
    }

    void MultiplayerSyncTab::drawClientSources( int32 sourceIndex )
    {
        EditorNetPlaySession* pSession = MultiplayerSyncTabInternal::findSession();
        if ( pSession == nullptr || pSession->isRunning() == false )
            return;
        // 0 번은 서버다. 클라이언트는 이력을 디버그 채널이 아니라 표준 입력 명령의 답(로그 한 줄)으로 낸다.
        for ( int32 processIndex = 1; processIndex < pSession->getProcessCount(); ++processIndex )
        {
            const EditorNetPlayProcessStatus status = pSession->getProcessStatus( processIndex );
            ImGui::PushID( processIndex );
            if ( processIndex > 1 )
                ImGui::SameLine();
            const bool bWaiting = _arrPendingProcess[sourceIndex] == processIndex;
            ImGui::BeginDisabled( status._bRunning == false || bWaiting );
            if ( ImGui::Button( status._label.c_str() ) )
            {
                const string command = "state.hashline " + to_string( MultiplayerSyncTabInternal::kClientHistoryTicks );
                if ( pSession->sendProcessCommand( processIndex, command ) )
                {
                    _arrPendingProcess[sourceIndex]     = processIndex;
                    _arrPendingReportCount[sourceIndex] = status._stateHashReportCount;
                }
                else
                {
                    SW_LOG_WARNING( "Sync could not ask %# for its history (the process has exited)", status._label );
                }
            }
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncClient" ) );
            ImGui::PopID();
        }
    }

    void MultiplayerSyncTab::pollClientReplies()
    {
        EditorNetPlaySession* pSession = MultiplayerSyncTabInternal::findSession();
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            const int32 processIndex = _arrPendingProcess[sourceIndex];
            if ( processIndex < 0 )
                continue;
            if ( pSession == nullptr || pSession->isRunning() == false )
            {
                _arrPendingProcess[sourceIndex] = -1; // 세션이 끝났다 — 답이 올 곳이 없다
                continue;
            }
            const EditorNetPlayProcessStatus status = pSession->getProcessStatus( processIndex );
            if ( status._stateHashReportCount == _arrPendingReportCount[sourceIndex] )
                continue;
            _arrPendingProcess[sourceIndex] = -1;
            if ( MultiplayerSyncTabInternal::loadClientReport( sourceIndex, status ) == false )
                SW_LOG_WARNING( "Sync could not take %#'s history: %#", status._label, MultiplayerSyncTabInternal::getCompare().getLastError() );
        }
    }

    void MultiplayerSyncTab::drawSource( int32 sourceIndex )
    {
        EditorStateHashCompare&      compare = MultiplayerSyncTabInternal::getCompare();
        const EditorStateHashSource& source  = compare.getSource( sourceIndex );
        ImGui::PushID( sourceIndex );
        ImGui::SeparatorText( MultiplayerSyncTabInternal::kArrSourceName[sourceIndex] );
        if ( source._listSample.empty() )
            ImGui::TextDisabled( "empty" );
        else
            ImGui::Text( "%s - %u tick(s), %s .. %s", source._label.c_str(), static_cast<uint32>( source._listSample.size() ),
                         to_string( source._listSample.front()._tick ).c_str(), to_string( source._listSample.back()._tick ).c_str() );

        if ( ImGui::Button( "Session server" ) )
        {
            string reason;
            if ( MultiplayerSyncTabInternal::loadSessionServer( sourceIndex, reason ) == false )
                SW_LOG_WARNING( "Sync could not read the session server's history: %#", reason );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncSessionServer" ) );
        ImGui::SameLine();
        drawClientSources( sourceIndex );

        ImGui::SetNextItemWidth( ImGui::GetFontSize() * MultiplayerSyncTabInternal::kFieldWidthInFonts );
        ImGui::InputTextWithHint( "##SyncAddress", "127.0.0.1:<port>", _arrAddress[sourceIndex].data(), _arrAddress[sourceIndex].capacity() );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * MultiplayerSyncTabInternal::kFieldWidthInFonts );
        ImGui::InputTextWithHint( "##SyncToken", "token", _arrToken[sourceIndex].data(), _arrToken[sourceIndex].capacity(), ImGuiInputTextFlags_Password );
        ImGui::SameLine();
        if ( ImGui::Button( "Read server" ) )
        {
            NetAddress remote;
            const bool bLoopback = NetAddress::parse( _arrAddress[sourceIndex].c_str(), 0, remote ) && remote.isValid() && remote == NetAddress::makeLoopback( remote._port );
            if ( bLoopback == false )
                SW_LOG_WARNING( "Sync reads only 127.0.0.1:<port> - '%#' is not one", _arrAddress[sourceIndex].c_str() );
            else if ( compare.loadFromChannel( sourceIndex, remote, _arrToken[sourceIndex].c_str() ) == false )
                SW_LOG_WARNING( "Sync could not read %#: %#", remote.toString(), compare.getLastError() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncReadServer" ) );

        ImGui::SetNextItemWidth( ImGui::GetFontSize() * MultiplayerSyncTabInternal::kPathWidthInFonts );
        ImGui::InputTextWithHint( "##SyncPath", "Saved/StateHashes/<name>.txt", _arrPath[sourceIndex].data(), _arrPath[sourceIndex].capacity() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load file" ) && compare.loadFromFile( sourceIndex, _arrPath[sourceIndex].c_str() ) == false )
            SW_LOG_WARNING( "Sync could not load %#: %#", _arrPath[sourceIndex].c_str(), compare.getLastError() );
        ImGui::SameLine();
        ImGui::BeginDisabled( source._listSample.empty() );
        if ( ImGui::Button( "Save" ) && compare.saveToFile( sourceIndex, _arrPath[sourceIndex].c_str() ) == false )
            SW_LOG_WARNING( "Sync could not write %#", _arrPath[sourceIndex].c_str() );
        ImGui::SameLine();
        if ( ImGui::Button( "Clear" ) )
            compare.clearSource( sourceIndex );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncFile" ) );
        ImGui::PopID();
    }

    void MultiplayerSyncTab::drawTab( const MultiplayerTabContext& /*context*/ )
    {
        pollClientReplies();
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            drawSource( sourceIndex );
        }
        ImGui::Separator();
        EditorStateHashCompare& compare = MultiplayerSyncTabInternal::getCompare();
        if ( ImGui::Button( "Compare" ) && compare.compare() == false )
            SW_LOG_WARNING( "Sync could not compare: %#", compare.getLastError() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncCompare" ) );
        if ( compare.isComparisonCurrent() )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( MultiplayerSyncTabInternal::describeComparison( compare.getComparison() ).c_str() );
        }
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerSyncTab, "sync", 400, "Sync" );
} // namespace sw::editor
