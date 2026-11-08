#include "pch.h"

#include "Core/File/ProcessRoots.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorStateHashCompare.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSyncModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Scene/SceneStateHash.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MultiplayerSyncTabInternal
        {
            /** @brief 슬롯 이름입니다(탭 · 명령 답). */
            static constexpr const utf8* kArrSourceName[EditorStateHashCompare::kSourceCount] = { "A", "B" };

            /** @brief 탭과 `netplay.sync` 가 같이 쓰는 비교입니다(창을 닫았다 열어도 남는다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorStateHashCompare& getCompare() { return getEditorState<EditorStateHashCompare>(); }

            /** @brief 클라이언트 답을 기다리는 상한(밀리초)입니다. 값은 환경설정 섹션 `EditorSyncPreferences` 가 준다. */
            static int64 getReplyTimeoutMilliseconds()
            {
                return static_cast<int64>( getPreferences<EditorSyncPreferences>()._clientReplyTimeoutSeconds * static_cast<float32>( constant::time::kMillisecondsPerSecond ) );
            }

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
            }

            /** @brief 그리기 · 폴링 경로의 세션입니다. 탭은 에디터 안에서만 그려지므로 에디터 문맥이 없으면 버그다(감시 값 · 개발 명령은 `findSession`). */
            static EditorNetPlaySession& getSession() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>(); }

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

            /** @brief 비교 결과 한 줄입니다(탭 · 명령 답). */
            static string describeComparison( const EditorStateHashComparison& comparison )
            {
                if ( comparison._commonTickCount == 0 )
                    return "the two histories share no tick - record them over the same ticks";
                if ( comparison._bDiverged )
                    return formatToString( "first divergent tick %# (%# common tick(s))", comparison._firstDivergentTick, comparison._commonTickCount );
                return formatToString( "no divergence over %# common tick(s)", comparison._commonTickCount );
            }

            /** @brief 감시 값 `Editor.NetPlaySyncCommonTicks` — 마지막 비교에서 두 이력에 모두 있던 틱 수입니다. */
            [[nodiscard]] static bool readCommonTicks( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getCompare().getComparison()._commonTickCount );
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlaySyncDiverged` — 마지막 비교에서 어긋난 틱을 찾았으면 1 입니다. */
            [[nodiscard]] static bool readDiverged( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = getCompare().getComparison()._bDiverged ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlaySyncObjectsCompared` — 마지막 "Which objects differ" 가 끝까지 갔으면 1 입니다(목록이 비어도 비교는 한 것). */
            [[nodiscard]] static bool readObjectsCompared( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = getCompare().hasObjectComparison() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlaySyncDifferingObjects` — 마지막 "Which objects differ" 가 짚은 다른 오브젝트 수입니다. */
            [[nodiscard]] static bool readDifferingObjects( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getCompare().getDifferingObjects().size() );
                return true;
            }

            /**
             * @brief `netplay.sync objects server|client` — 탭의 "Which objects differ" 와 같은 비교를 합니다. 서버 칸은 두 칸을 세션 서버로 두고 채널에서 다시 읽고(`compareObjectsFromChannels`),
             *        클라이언트 칸은 A 를 세션 서버(채널), B 를 첫 클라이언트(표준 입력의 마지막 `state.objectline` 답)로 두고 `compareObjects` 로 견준다.
             * @details 두 쪽의 틱이 같아야 하므로 서버를 `pause` 한 뒤 부른다. 클라이언트 칸은 답이 서버 틱에 닿지 않았으면 그 틱의 답을 묻고(`EditorNetPlaySession::requestObjectHashes`) 돌아온다 —
             *          받은 답 수(`Editor.NetPlayObjectHashReports`)가 바뀐 뒤 다시 부르면 비교한다(탭은 단추에서 같은 요청을 하고 답을 기다린다).
             */
            static bool runObjects( const string& side, string& outReply )
            {
                const bool bServer = side == "server";
                if ( bServer == false && side != "client" )
                    return false;
                EditorStateHashCompare& compare = getCompare();
                bool                    bDone   = false;
                if ( loadSessionServer( 0, outReply ) == false )
                    return true;
                if ( bServer )
                {
                    bDone = loadSessionServer( 1, outReply ) && compare.compareObjectsFromChannels();
                }
                else
                {
                    EditorNetPlaySession* pSession   = findSession();
                    const bool            bHasClient = pSession != nullptr && pSession->isRunning() && pSession->getProcessCount() > 1;
                    if ( bHasClient == false )
                    {
                        outReply = "netplay sync objects client needs a running session with a client";
                        return true;
                    }
                    uint64                  serverTick = 0;
                    vector<SceneObjectHash> listServer;
                    if ( compare.readObjectsFromChannel( 0, serverTick, listServer ) == false )
                    {
                        outReply = compare.getLastError();
                        return true;
                    }
                    // 클라이언트 답이 서버 틱에 닿지 않았으면 그 틱의 답을 묻는다(세션이 이른 답을 거르고 다시 묻는다) — 받은 답 수가 바뀐 뒤 이 명령을 다시 부른다.
                    const EditorNetPlayProcessStatus status = pSession->getProcessStatus( 1 );
                    if ( status._objectHashReportCount == 0 || status._objectHashTick < serverTick )
                    {
                        outReply = pSession->requestObjectHashes( 1, serverTick )
                                     ? formatToString( "asked %# for its object hashes at tick %# - wait until Editor.NetPlayObjectHashReports changes, then run this again", status._label, serverTick )
                                     : "netplay sync objects client could not ask the client (its process is gone)";
                        return true;
                    }
                    bDone = compare.compareObjects( serverTick, listServer, status._objectHashTick, status._listObjectHash );
                }
                if ( bDone )
                    outReply = formatToString( "netplay sync objects tick %#: %# differing object(s)", compare.getObjectTick(), compare.getDifferingObjects().size() );
                else
                    outReply = compare.getLastError();
                return true;
            }

            /**
             * @brief `netplay.sync save <경로>` — 세션 전용 서버의 이력을 A 에 담아 파일로 씁니다. `netplay.sync compare <경로>` — 세션 서버를 A, 파일을 B 로 담아 비교합니다.
             * @details 탭의 "Session server" · "Load file" · "Save" · "Compare" 와 같은 길이다. 시나리오는 이 명령과 감시 값 `Editor.NetPlaySync*` 로 확인한다.
             */
            static bool runSync( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() == 2 && listArgument[0] == "objects" )
                    return runObjects( listArgument[1], outReply );
                if ( listArgument.size() == 2 && listArgument[0] == "channel" )
                {
                    EditorStateHashCompare& channelCompare = getCompare();
                    if ( channelCompare.setHashChannel( listArgument[1] ) )
                        outReply = "netplay sync reads the hash line '" + channelCompare.getHashChannel() + "'";
                    else
                        outReply = channelCompare.getLastError();
                    return true;
                }
                const bool bSave    = listArgument.size() == 2 && listArgument[0] == "save";
                const bool bCompare = listArgument.size() == 2 && listArgument[0] == "compare";
                if ( bSave == false && bCompare == false )
                    return false;
                string filePath;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[1], filePath, outReply ) == false )
                    return true;
                EditorStateHashCompare& compare = getCompare();
                if ( loadSessionServer( 0, outReply ) == false )
                    return true;
                if ( bSave )
                {
                    const bool bSaved = compare.saveToFile( 0, filePath );
                    outReply          = bSaved ? "netplay sync saved " + to_string( compare.getSource( 0 )._listSample.size() ) + " tick(s) to " + filePath
                                               : "netplay sync could not write " + filePath;
                    return true;
                }
                if ( compare.loadFromFile( 1, filePath ) == false || compare.compare() == false )
                {
                    outReply = compare.getLastError();
                    return true;
                }
                outReply = "netplay sync " + describeComparison( compare.getComparison() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_SETTINGS( EditorSyncPreferences, "multiplayer_sync", "Editor/Multiplayer Sync", 370, nullptr );

    SW_AUTOMATION_WATCH( editorNetPlaySyncCommonTicks, "Editor.NetPlaySyncCommonTicks", "Ticks both state hash histories of the multiplayer Sync tab held at the last comparison",
                         &MultiplayerSyncTabInternal::readCommonTicks );
    SW_AUTOMATION_WATCH( editorNetPlaySyncDiverged, "Editor.NetPlaySyncDiverged", "1 when the multiplayer Sync tab's last comparison found a divergent tick",
                         &MultiplayerSyncTabInternal::readDiverged );
    SW_AUTOMATION_WATCH( editorNetPlaySyncObjectsCompared, "Editor.NetPlaySyncObjectsCompared", "1 when the Sync tab's last 'Which objects differ' compared the two object hash lists",
                         &MultiplayerSyncTabInternal::readObjectsCompared );
    SW_AUTOMATION_WATCH( editorNetPlaySyncDifferingObjects, "Editor.NetPlaySyncDifferingObjects", "Objects the Sync tab's last 'Which objects differ' found different",
                         &MultiplayerSyncTabInternal::readDifferingObjects );
    SW_DEV_COMMAND( NetPlaySync, "netplay.sync", "netplay.sync save|compare <path> | netplay.sync objects server|client | netplay.sync channel <name>",
                    "Save the session server's per-tick state hash history to a file, or compare it with a saved one (first divergent tick), or list the objects that differ (Which objects differ), or pick the hash line to read (scene, or a kit's such as destruction · rollback)",
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
        /**
         * @brief 되감기 줄(서버 디버깅 S7 · O7)입니다. 세션이 없으면 `.swintent` 로 멈춘 재생 서버를 띄우고, 재생 서버가 떠 있으면 `step N` 으로 한 틱씩 진행합니다.
         * @details 어긋난 틱을 안 뒤 새 서버를 틱 0 에서 멈춘 채 띄워(`startReplayServer`) 그 틱까지 정확히 진행하면, 같은 틱에서 Which objects differ · Inspector 로 볼 수 있다.
         */
        void drawReplay();

    private:
        fixed_string<constant::buffer::kMax64>     _arrChannelName;                                   ///< 읽을 해시 줄 이름(편집 필드, `EditorStateHashCompare::setHashChannel`)
        fixed_string<constant::file::kMaxPathSize> _arrPath[EditorStateHashCompare::kSourceCount];    ///< 슬롯마다 파일 경로(편집 필드)
        fixed_string<constant::buffer::kMax64>     _arrAddress[EditorStateHashCompare::kSourceCount]; ///< 슬롯마다 채널 주소(편집 필드)
        fixed_string<constant::buffer::kMax128>    _arrToken[EditorStateHashCompare::kSourceCount];   ///< 슬롯마다 채널 토큰(편집 필드)
        fixed_string<constant::file::kMaxPathSize> _arrReplayPath;                                    ///< 재생할 `.swintent` 경로(편집 필드)
        MultiplayerSyncModel                       _model;                                            ///< 클라이언트 답 기다리기
        int32                                      _replayStepCount;                                  ///< 재생 서버를 한 번에 진행할 틱 수(편집 필드)
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSyncTab::MultiplayerSyncTab()
        : _arrChannelName{}
        , _arrPath{}
        , _arrAddress{}
        , _arrToken{}
        , _arrReplayPath{}
        , _model{}
        , _replayStepCount{ 1 }
    {
    }

    void MultiplayerSyncTab::drawClientSources( int32 sourceIndex )
    {
        EditorNetPlaySession& session = MultiplayerSyncTabInternal::getSession();
        if ( session.isRunning() == false )
            return;
        // 0 번은 서버다. 클라이언트는 이력을 디버그 채널이 아니라 표준 입력 명령의 답(로그 한 줄)으로 낸다.
        for ( int32 processIndex = 1; processIndex < session.getProcessCount(); ++processIndex )
        {
            const EditorNetPlayProcessStatus status = session.getProcessStatus( processIndex );
            ImGui::PushID( processIndex );
            if ( processIndex > 1 )
                ImGui::SameLine();
            const bool bWaiting = _model.isWaitingForHistory( sourceIndex, processIndex );
            ImGui::BeginDisabled( status._bRunning == false || bWaiting );
            if ( ImGui::Button( status._label.c_str() ) )
                (void)_model.requestClientHistory( session.getNetPlayProcesses(), MultiplayerSyncTabInternal::getCompare(), sourceIndex, processIndex ); // 실패는 모델이 알린다
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncClient" ) );
            ImGui::PopID();
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
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "TickS", TextArgumentList().addText( "label", source._label.c_str() ).addInteger( "sampleCount", static_cast<int64>( source._listSample.size() ) ).addText( "tick", to_string( source._listSample.front()._tick ).c_str() ).addText( "tick2", to_string( source._listSample.back()._tick ).c_str() ) ).c_str() );

        if ( ImGui::Button( SW_LOCKEY( "EdText", "SessionServer" ) ) )
        {
            string reason;
            if ( MultiplayerSyncTabInternal::loadSessionServer( sourceIndex, reason ) == false )
                SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotRead", "Sync could not read the session server's history: %#", reason );
        }
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
        {
            NetAddress remote;
            const bool bLoopback = NetAddress::parse( _arrAddress[sourceIndex].c_str(), 0, remote ) && remote.isValid() && remote.isLoopback();
            if ( bLoopback == false )
                SW_LOG_WARNING_ID( "MultiplayerSync.SyncReadsOnlyPort", "Sync reads only 127.0.0.1:<port> - '%#' is not one", _arrAddress[sourceIndex].c_str() );
            else if ( compare.loadFromChannel( sourceIndex, remote, _arrToken[sourceIndex].c_str() ) == false )
                SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotRead2", "Sync could not read %#: %#", remote.toString(), compare.getLastError() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncReadServer" ) );

        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldPathEm" ) );
        ImGui::InputTextWithHint( "##SyncPath", "StateHashes/<name>.txt", _arrPath[sourceIndex].data(), _arrPath[sourceIndex].capacity() );
        // 사람이 친 상대 경로는 이 실행의 산출물 폴더(`ProcessRoots::_saved`) 아래다 - 작업 폴더가 아니다. 경로가 아니면(드라이브 상대) 빈 값이라 읽기 · 쓰기가 실패로 알린다.
        const optional<AbsolutePath> typedPath = AbsolutePath::tryResolve( _arrPath[sourceIndex].c_str(), ProcessRoots::get()._saved );
        const string                 filePath  = typedPath.has_value() ? typedPath->str() : string{};
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "LoadFile" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "LoadFile" ) ) && compare.loadFromFile( sourceIndex, filePath ) == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotLoad", "Sync could not load %#: %#", _arrPath[sourceIndex].c_str(), compare.getLastError() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        ImGui::BeginDisabled( source._listSample.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) && compare.saveToFile( sourceIndex, filePath ) == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotWrite", "Sync could not write %#", _arrPath[sourceIndex].c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Clear" ) ) )
            compare.clearSource( sourceIndex );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncFile" ) );
        ImGui::PopID();
    }

    void MultiplayerSyncTab::drawReplay()
    {
        EditorNetPlaySession& session = MultiplayerSyncTabInternal::getSession();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldPathEm" ) );
        ImGui::InputTextWithHint( "##SyncReplayPath", "<name>.swintent", _arrReplayPath.data(), _arrReplayPath.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ReplayServer" ) ) );
        ImGui::BeginDisabled( session.isRunning() || _arrReplayPath.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "ReplayServer" ) ) && session.startReplayServer( _arrReplayPath.c_str() ) == false )
            _model.setError( "The replay server could not start - see the Output Log" );
        ImGui::EndDisabled();
        // 한 틱씩 진행은 떠 있는 서버가 있을 때만 — 전용 서버는 멈춘 채 떠서 `step` 만큼만 돈다.
        ImGui::BeginDisabled( session.isRunning() == false );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldStepEm" ) );
        ImGui::InputInt( "##SyncReplayStep", &_replayStepCount, 1, 10 );
        _replayStepCount = _replayStepCount < 1 ? 1 : _replayStepCount;
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Step" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Step" ) ) && session.sendServerCommand( formatToString( "step %#", _replayStepCount ) ) == false )
            _model.setError( "The server did not take the step command" );
        ImGui::EndDisabled();
    }

    void MultiplayerSyncTab::drawTab( const MultiplayerTabContext& /*context*/ )
    {
        const int64 timeoutMilliseconds = MultiplayerSyncTabInternal::getReplyTimeoutMilliseconds();
        _model.poll( MultiplayerSyncTabInternal::getSession().getNetPlayProcesses(), MultiplayerSyncTabInternal::getCompare(), timeoutMilliseconds );
        // 어느 줄의 이력을 읽을지 — 씬 줄(`scene`) 말고 키트가 더한 줄(`destruction` · `rollback`)도 같은 비교를 탄다. 읽는 단추를 누를 때 이 이름을 쓴다.
        if ( _arrChannelName.empty() )
            _arrChannelName = MultiplayerSyncTabInternal::getCompare().getHashChannel().c_str();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" ) );
        if ( ImGui::InputTextWithHint( "##SyncChannel", "scene | destruction | rollback", _arrChannelName.data(), _arrChannelName.capacity() ) )
        {
            if ( MultiplayerSyncTabInternal::getCompare().setHashChannel( _arrChannelName.c_str() ) == false )
                _model.setError( MultiplayerSyncTabInternal::getCompare().getLastError() );
            else
                _model.clearError();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncChannel" ) );
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            drawSource( sourceIndex );
        }
        ImGui::Separator();
        drawReplay();
        if ( _model.getError().empty() == false )
            EditorThemeUtil::textError( _model.getError().c_str() );
        EditorStateHashCompare& compare = MultiplayerSyncTabInternal::getCompare();
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Compare" ) ) && compare.compare() == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotCompare2", "Sync could not compare: %#", compare.getLastError() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncCompare" ) );
        if ( compare.isComparisonCurrent() )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( MultiplayerSyncTabInternal::describeComparison( compare.getComparison() ).c_str() );
        }
        // 어긋난 틱 근처에서 두 서버를 pause 한 채 누른다 — 두 서버의 현재 오브젝트 해시를 다시 받아 다른 오브젝트를 짚는다.
        if ( ImGui::Button( SW_LOCKEY( "EdText", "WhichObjectsDiffer" ) ) )
            _model.startObjectCompare( MultiplayerSyncTabInternal::getSession().getNetPlayProcesses(), compare, timeoutMilliseconds );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncObjects" ) );
        if ( compare.hasObjectComparison() )
        {
            ImGui::TextUnformatted( ( formatToString( "tick %#", compare.getObjectTick() ) ).c_str() );
            if ( compare.getDifferingObjects().empty() )
                ImGui::TextUnformatted( "every named object matches" );
            for ( const string& name : compare.getDifferingObjects() )
            {
                ImGui::BulletText( "%s", name.c_str() );
            }
        }
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerSyncTab, "sync", 400, "Sync" );
} // namespace sw::editor
