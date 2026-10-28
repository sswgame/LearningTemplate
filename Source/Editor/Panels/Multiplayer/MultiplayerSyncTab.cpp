#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Config/EditorSyncPreferences.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorStateHashCompare.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
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
            /** @brief 재생 서버 진행 틱 수 칸의 너비(글자 크기 배수)입니다. */
            static constexpr float32 kStepFieldWidthInFonts = 6.0f;
            /** @brief 클라이언트에게 묻는 최근 틱 수입니다. 답은 로그 한 줄이라 한 줄 상한(`DebugReplyLine::kMaxBytes`)에 드는 수까지만 묻는다(더 많이 물으면 클라이언트가 오류로 답한다). 세션 서버 이력(3600 틱)보다 짧다. */
            static constexpr int32 kClientHistoryTicks = static_cast<int32>( StateHashHistory::kMaxReportLineSamples );
            /** @brief 슬롯 이름입니다(탭 · 명령 답). */
            static constexpr const utf8* kArrSourceName[EditorStateHashCompare::kSourceCount] = { "A", "B" };

            /** @brief 탭과 `netplay.sync` 가 같이 쓰는 비교입니다(창을 닫았다 열어도 남는다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorStateHashCompare& getCompare() { return getEditorState<EditorStateHashCompare>(); }

            /** @brief 클라이언트 답을 기다리는 상한(밀리초)입니다. 값은 환경설정 섹션 `EditorSyncPreferences` 가 준다. */
            static int64 getReplyTimeoutMilliseconds()
            {
                return static_cast<int64>( getPreferences<EditorSyncPreferences>()._clientReplyTimeoutSeconds * static_cast<float32>( constant::kMillisecondsPerSecond ) );
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
             * @details 두 쪽의 틱이 같아야 하므로 서버를 `pause` 하고 클라이언트가 마지막 틱을 받은 뒤에 부른다. 클라이언트 칸은 먼저 `netplay.command client1 state.objectline` 로 답을 받아 둔다(탭은 단추에서 묻고 답을 기다린다).
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
                    EditorNetPlaySession*      pSession   = findSession();
                    const bool                 bHasClient = pSession != nullptr && pSession->isRunning() && pSession->getProcessCount() > 1;
                    EditorNetPlayProcessStatus status     = bHasClient ? pSession->getProcessStatus( 1 ) : EditorNetPlayProcessStatus{};
                    if ( bHasClient == false || status._objectHashReportCount == 0 )
                    {
                        outReply = "netplay sync objects client has no state.objectline answer yet - ask with netplay.command client1 state.objectline first";
                        return true;
                    }
                    uint64                  serverTick = 0;
                    vector<SceneObjectHash> listServer;
                    bDone = compare.readObjectsFromChannel( 0, serverTick, listServer ) && compare.compareObjects( serverTick, listServer, status._objectHashTick, status._listObjectHash );
                }
                if ( bDone )
                    outReply = "netplay sync objects tick " + to_string( compare.getObjectTick() ) + ": " + to_string( compare.getDifferingObjects().size() ) + " differing object(s)";
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
        /** @brief 기다리는 클라이언트 답이 왔으면 칸에 담습니다. 프레임마다 부릅니다. */
        void pollClientReplies();
        /**
         * @brief "Which objects differ" 를 시작합니다. 채널에서 읽은 칸은 바로 받고, 클라이언트에서 읽은 칸은 표준 입력에 `state.objectline` 을 물어 답을 기다린다(`pollObjectReplies`).
         * @details 두 칸의 답이 모이면 틱 머리 검증을 거치는 `EditorStateHashCompare::compareObjects` 하나로 견준다(서버 칸과 같은 규칙).
         */
        void startObjectCompare();
        /** @brief 기다리는 클라이언트의 오브젝트 해시 답이 왔으면 담고, 두 칸이 모두 모였으면 견줍니다. 프레임마다 부릅니다. */
        void pollObjectReplies();
        /**
         * @brief 되감기 줄(서버 디버깅 S7 · O7)입니다. 세션이 없으면 `.swintent` 로 멈춘 재생 서버를 띄우고, 재생 서버가 떠 있으면 `step N` 으로 한 틱씩 진행합니다.
         * @details 어긋난 틱을 안 뒤 새 서버를 틱 0 에서 멈춘 채 띄워(`startReplayServer`) 그 틱까지 정확히 진행하면, 같은 틱에서 Which objects differ · Inspector 로 볼 수 있다.
         */
        void drawReplay();

        fixed_string<constant::kMaxBuffer64>  _arrChannelName;                                                ///< 읽을 해시 줄 이름(편집 필드, `EditorStateHashCompare::setHashChannel`)
        fixed_string<constant::kMaxPathSize>  _arrPath[EditorStateHashCompare::kSourceCount];                 ///< 슬롯마다 파일 경로(편집 필드)
        fixed_string<constant::kMaxBuffer64>  _arrAddress[EditorStateHashCompare::kSourceCount];              ///< 슬롯마다 채널 주소(편집 필드)
        fixed_string<constant::kMaxBuffer128> _arrToken[EditorStateHashCompare::kSourceCount];                ///< 슬롯마다 채널 토큰(편집 필드)
        int32                                 _arrPendingProcess[EditorStateHashCompare::kSourceCount];       ///< 슬롯마다 답을 기다리는 클라이언트 프로세스 번호(-1 이면 없다)
        uint32                                _arrPendingReportCount[EditorStateHashCompare::kSourceCount];   ///< 물을 때의 그 프로세스 이력 답 수 — 늘면 새 답이다
        bool                                  _bObjectRequest;                                                ///< "Which objects differ" 가 클라이언트 답을 기다리는 중이다
        int32                                 _arrObjectPendingProcess[EditorStateHashCompare::kSourceCount]; ///< 슬롯마다 오브젝트 해시 답을 기다리는 클라이언트 프로세스 번호(-1 이면 안 기다린다)
        uint32                                _arrObjectPendingCount[EditorStateHashCompare::kSourceCount];   ///< 물을 때의 그 프로세스 오브젝트 해시 답 수
        uint64                                _arrObjectTick[EditorStateHashCompare::kSourceCount];           ///< 슬롯마다 받은 오브젝트 해시의 틱
        vector<SceneObjectHash>               _arrObjectList[EditorStateHashCompare::kSourceCount];           ///< 슬롯마다 받은 오브젝트별 해시
        Stopwatch                             _arrPendingWatch[EditorStateHashCompare::kSourceCount];         ///< 슬롯마다 이력 질의를 보낸 뒤 지난 시간 — 상한(`getReplyTimeoutMilliseconds`)을 넘으면 오류로 끝낸다
        Stopwatch                             _objectWatch;                                                   ///< 오브젝트 해시 질의를 보낸 뒤 지난 시간(위와 같다)
        string                                _errorText;                                                     ///< 마지막 질의 오류(시간 초과 포함). 새 질의가 시작되면 비운다
        fixed_string<constant::kMaxPathSize>  _arrReplayPath;                                                 ///< 재생할 `.swintent` 경로(편집 필드)
        int32                                 _replayStepCount;                                               ///< 재생 서버를 한 번에 진행할 틱 수(편집 필드)
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSyncTab::MultiplayerSyncTab()
        : _arrChannelName{}
        , _arrPath{}
        , _arrAddress{}
        , _arrToken{}
        , _arrPendingProcess{ -1, -1 }
        , _arrPendingReportCount{}
        , _bObjectRequest{ false }
        , _arrObjectPendingProcess{ -1, -1 }
        , _arrObjectPendingCount{}
        , _arrObjectTick{}
        , _arrObjectList{}
        , _arrPendingWatch{}
        , _objectWatch{}
        , _errorText{}
        , _arrReplayPath{}
        , _replayStepCount{ 1 }
    {
        static_assert( EditorStateHashCompare::kSourceCount == 2, "the pending client table is initialized for two sources" );
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
            const bool bWaiting = _arrPendingProcess[sourceIndex] == processIndex;
            ImGui::BeginDisabled( status._bRunning == false || bWaiting );
            if ( ImGui::Button( status._label.c_str() ) )
            {
                const string& channel = MultiplayerSyncTabInternal::getCompare().getHashChannel();
                const string  command = "state.hashline " + to_string( MultiplayerSyncTabInternal::kClientHistoryTicks ) + ( channel == StateHashHistory::kSceneChannelName ? string{} : " " + channel );
                if ( session.sendProcessCommand( processIndex, command ) )
                {
                    _arrPendingProcess[sourceIndex]     = processIndex;
                    _arrPendingReportCount[sourceIndex] = status._stateHashReportCount;
                    _arrPendingWatch[sourceIndex].restart();
                    _errorText.clear();
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
        EditorNetPlaySession& session = MultiplayerSyncTabInternal::getSession();
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            const int32 processIndex = _arrPendingProcess[sourceIndex];
            if ( processIndex < 0 )
                continue;
            if ( session.isRunning() == false )
            {
                _arrPendingProcess[sourceIndex] = -1; // 세션이 끝났다 — 답이 올 곳이 없다
                continue;
            }
            const EditorNetPlayProcessStatus status = session.getProcessStatus( processIndex );
            if ( status._stateHashReportCount == _arrPendingReportCount[sourceIndex] )
            {
                // 클라이언트는 잘못된 질문에 오류 줄로 답하고 그 답은 이력 답 수를 늘리지 않는다 - 상한 없이 기다리면 영원히 대기 상태로 남는다.
                if ( _arrPendingWatch[sourceIndex].getElapsedMilliseconds() >= MultiplayerSyncTabInternal::getReplyTimeoutMilliseconds() )
                {
                    _arrPendingProcess[sourceIndex] = -1;
                    _errorText                      = "Timed out waiting for " + status._label + "'s history (no valid answer within " + to_string( MultiplayerSyncTabInternal::getReplyTimeoutMilliseconds() ) + " ms)";
                    SW_LOG_WARNING( "Sync %#", _errorText );
                }
                continue;
            }
            _arrPendingProcess[sourceIndex] = -1;
            if ( MultiplayerSyncTabInternal::loadClientReport( sourceIndex, status ) == false )
                SW_LOG_WARNING( "Sync could not take %#'s history: %#", status._label, MultiplayerSyncTabInternal::getCompare().getLastError() );
            else
                MultiplayerSyncTabInternal::getCompare().setClientOrigin( sourceIndex, processIndex );
        }
    }

    void MultiplayerSyncTab::startObjectCompare()
    {
        EditorStateHashCompare& compare = MultiplayerSyncTabInternal::getCompare();
        EditorNetPlaySession&   session = MultiplayerSyncTabInternal::getSession();
        _bObjectRequest                 = false;
        bool bWaiting                   = false;
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            _arrObjectPendingProcess[sourceIndex] = -1;
            const int32 processIndex              = compare.getClientOrigin( sourceIndex );
            if ( processIndex >= 0 )
            {
                // 클라이언트는 디버그 채널이 없다 - 표준 입력의 한 줄 답(`state.objectline`)을 기다린다.
                if ( session.isRunning() == false || session.sendProcessCommand( processIndex, "state.objectline" ) == false )
                {
                    SW_LOG_WARNING( "Sync could not ask slot %# for its object hashes (the session or the process is gone)", sourceIndex );
                    return;
                }
                _arrObjectPendingProcess[sourceIndex] = processIndex;
                _arrObjectPendingCount[sourceIndex]   = session.getProcessStatus( processIndex )._objectHashReportCount;
                bWaiting                              = true;
                continue;
            }
#if SW_DEV_COMMANDS_ENABLED
            if ( compare.readObjectsFromChannel( sourceIndex, _arrObjectTick[sourceIndex], _arrObjectList[sourceIndex] ) == false )
            {
                SW_LOG_WARNING( "Sync could not compare objects: %#", compare.getLastError() );
                return;
            }
#else
            SW_LOG_WARNING( "Sync has no debug channel in this build - object hashes come only from session clients" );
            return;
#endif
        }
        _bObjectRequest = true;
        _errorText.clear();
        _objectWatch.restart();
        if ( bWaiting == false )
            pollObjectReplies();
    }

    void MultiplayerSyncTab::pollObjectReplies()
    {
        if ( _bObjectRequest == false )
            return;
        EditorNetPlaySession& session = MultiplayerSyncTabInternal::getSession();
        bool                  bAllIn  = true;
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            const int32 processIndex = _arrObjectPendingProcess[sourceIndex];
            if ( processIndex < 0 )
                continue;
            if ( session.isRunning() == false )
            {
                _bObjectRequest = false; // 세션이 끝났다 - 답이 올 곳이 없다
                return;
            }
            const EditorNetPlayProcessStatus status = session.getProcessStatus( processIndex );
            if ( status._objectHashReportCount == _arrObjectPendingCount[sourceIndex] )
            {
                bAllIn = false;
                continue;
            }
            _arrObjectTick[sourceIndex]           = status._objectHashTick;
            _arrObjectList[sourceIndex]           = status._listObjectHash;
            _arrObjectPendingProcess[sourceIndex] = -1;
        }
        if ( bAllIn == false )
        {
            if ( _objectWatch.getElapsedMilliseconds() >= MultiplayerSyncTabInternal::getReplyTimeoutMilliseconds() )
            {
                _bObjectRequest = false;
                for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
                {
                    _arrObjectPendingProcess[sourceIndex] = -1;
                }
                _errorText = "Timed out waiting for the object hashes (no valid answer within " + to_string( MultiplayerSyncTabInternal::getReplyTimeoutMilliseconds() ) + " ms)";
                SW_LOG_WARNING( "Sync %#", _errorText );
            }
            return;
        }
        _bObjectRequest                 = false;
        EditorStateHashCompare& compare = MultiplayerSyncTabInternal::getCompare();
        if ( compare.compareObjects( _arrObjectTick[0], _arrObjectList[0], _arrObjectTick[1], _arrObjectList[1] ) == false )
            SW_LOG_WARNING( "Sync could not compare objects: %#", compare.getLastError() );
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
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Read server" ) );
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
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load file" ) );
        if ( ImGui::Button( "Load file" ) && compare.loadFromFile( sourceIndex, _arrPath[sourceIndex].c_str() ) == false )
            SW_LOG_WARNING( "Sync could not load %#: %#", _arrPath[sourceIndex].c_str(), compare.getLastError() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        ImGui::BeginDisabled( source._listSample.empty() );
        if ( ImGui::Button( "Save" ) && compare.saveToFile( sourceIndex, _arrPath[sourceIndex].c_str() ) == false )
            SW_LOG_WARNING( "Sync could not write %#", _arrPath[sourceIndex].c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Clear" ) );
        if ( ImGui::Button( "Clear" ) )
            compare.clearSource( sourceIndex );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncFile" ) );
        ImGui::PopID();
    }

    void MultiplayerSyncTab::drawReplay()
    {
        EditorNetPlaySession& session = MultiplayerSyncTabInternal::getSession();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * MultiplayerSyncTabInternal::kPathWidthInFonts );
        ImGui::InputTextWithHint( "##SyncReplayPath", "<name>.swintent", _arrReplayPath.data(), _arrReplayPath.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Replay server" ) );
        ImGui::BeginDisabled( session.isRunning() || _arrReplayPath.empty() );
        if ( ImGui::Button( "Replay server" ) && session.startReplayServer( _arrReplayPath.c_str() ) == false )
            _errorText = "The replay server could not start - see the Output Log";
        ImGui::EndDisabled();
        // 한 틱씩 진행은 떠 있는 서버가 있을 때만 — 전용 서버는 멈춘 채 떠서 `step` 만큼만 돈다.
        ImGui::BeginDisabled( session.isRunning() == false );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * MultiplayerSyncTabInternal::kStepFieldWidthInFonts );
        ImGui::InputInt( "##SyncReplayStep", &_replayStepCount, 1, 10 );
        _replayStepCount = _replayStepCount < 1 ? 1 : _replayStepCount;
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Step" ) );
        if ( ImGui::Button( "Step" ) && session.sendServerCommand( "step " + to_string( _replayStepCount ) ) == false )
            _errorText = "The server did not take the step command";
        ImGui::EndDisabled();
    }

    void MultiplayerSyncTab::drawTab( const MultiplayerTabContext& /*context*/ )
    {
        pollClientReplies();
        pollObjectReplies();
        // 어느 줄의 이력을 읽을지 — 씬 줄(`scene`) 말고 키트가 더한 줄(`destruction` · `rollback`)도 같은 비교를 탄다. 읽는 단추를 누를 때 이 이름을 쓴다.
        if ( _arrChannelName.empty() )
            _arrChannelName = MultiplayerSyncTabInternal::getCompare().getHashChannel().c_str();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * MultiplayerSyncTabInternal::kFieldWidthInFonts );
        if ( ImGui::InputTextWithHint( "##SyncChannel", "scene | destruction | rollback", _arrChannelName.data(), _arrChannelName.capacity() ) )
        {
            if ( MultiplayerSyncTabInternal::getCompare().setHashChannel( _arrChannelName.c_str() ) == false )
                _errorText = MultiplayerSyncTabInternal::getCompare().getLastError();
            else
                _errorText.clear();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncChannel" ) );
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            drawSource( sourceIndex );
        }
        ImGui::Separator();
        drawReplay();
        if ( _errorText.empty() == false )
            EditorThemeUtil::textError( _errorText.c_str() );
        EditorStateHashCompare& compare = MultiplayerSyncTabInternal::getCompare();
        if ( ImGui::Button( "Compare" ) && compare.compare() == false )
            SW_LOG_WARNING( "Sync could not compare: %#", compare.getLastError() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncCompare" ) );
        if ( compare.isComparisonCurrent() )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( MultiplayerSyncTabInternal::describeComparison( compare.getComparison() ).c_str() );
        }
#if SW_DEV_COMMANDS_ENABLED
        // 어긋난 틱 근처에서 두 서버를 pause 한 채 누른다 — 두 서버의 현재 오브젝트 해시를 다시 받아 다른 오브젝트를 짚는다.
        if ( ImGui::Button( "Which objects differ" ) )
            startObjectCompare();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "SyncObjects" ) );
        if ( compare.hasObjectComparison() )
        {
            ImGui::TextUnformatted( ( "tick " + to_string( compare.getObjectTick() ) ).c_str() );
            if ( compare.getDifferingObjects().empty() )
                ImGui::TextUnformatted( "every named object matches" );
            for ( const string& name : compare.getDifferingObjects() )
            {
                ImGui::BulletText( "%s", name.c_str() );
            }
        }
#endif
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerSyncTab, "sync", 400, "Sync" );
} // namespace sw::editor
