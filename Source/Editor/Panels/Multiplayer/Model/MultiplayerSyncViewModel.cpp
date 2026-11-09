#include "pch.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerSyncViewModel.h"

#include "Core/File/ProcessRoots.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/NetPlay/EditorStateHashCompare.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSyncModel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Scene/SceneObjectHash.h"

namespace sw::editor
{
    static_assert( MultiplayerSyncViewModel::kSourceCount == EditorStateHashCompare::kSourceCount, "the view model's slot count follows the comparison" );

    namespace
    {
        struct MultiplayerSyncViewModelInternal
        {
            /** @brief 탭과 `netplay.sync` 가 같이 쓰는 비교입니다(창을 닫았다 열어도 남는다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorStateHashCompare& getCompare() { return getEditorState<EditorStateHashCompare>(); }

            /** @brief 클라이언트 답을 기다리는 상한(밀리초)입니다. 값은 환경설정 섹션 `EditorSyncPreferences` 가 준다. */
            static int64 getReplyTimeoutMilliseconds()
            {
                return static_cast<int64>( getPreferences<EditorSyncPreferences>()._clientReplyTimeoutSeconds * static_cast<float32>( constant::time::kMillisecondsPerSecond ) );
            }

            /** @brief 사람이 친 경로를 이 실행의 산출물 폴더(`ProcessRoots::_saved`) 아래로 풉니다 — 작업 폴더가 아니다. 경로가 아니면(드라이브 상대) 빈 값이라 읽기 · 쓰기가 실패로 알린다. */
            static string resolveTypedPath( string_view typedPath )
            {
                const optional<AbsolutePath> resolved = AbsolutePath::tryResolve( typedPath, ProcessRoots::get()._saved );
                return resolved.has_value() ? resolved->str() : string{};
            }

            /** @brief 세션이 띄운 전용 서버의 이력을 칸에 담습니다. 서버나 채널이 없으면 그 이유입니다(채널 읽기 실패는 비교기의 글이 대상). */
            [[nodiscard]] static expected<void> loadSessionServer( int32 sourceIndex )
            {
                EditorNetPlaySession* pSession   = MultiplayerSessionViewModel::findSession();
                const bool            bHasServer = pSession != nullptr && pSession->isRunning() && pSession->getDebugChannelPort() != 0;
                if ( bHasServer == false )
                    return unexpected( Error{ ErrorKind::NotFound, "no dedicated server session with a debug channel is running" } );
                EditorStateHashCompare& compare = getCompare();
                if ( compare.loadFromChannel( sourceIndex, NetAddress::makeLoopback( pSession->getDebugChannelPort() ), pSession->getDebugChannelToken() ) == false )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "the session server's history could not be read", compare.getLastError() } );
                return {};
            }

            /** @brief 개발 명령 답 글로 씁니다(`<문장>: <대상>`). */
            static string makeReply( const Error& error )
            {
                return error.detail().empty() ? string( error.message() ) : string( error.message() ) + ": " + string( error.detail() );
            }

            /** @brief 비교 결과 한 줄입니다(탭 · 명령 답). */
            static string describeComparison( const EditorStateHashComparison& comparison )
            {
                if ( comparison._commonTickCount == 0 )
                    return "the two histories share no tick - record them over the same ticks";
                if ( comparison._bDiverged )
                {
                    StringBuilder<constant::buffer::kMax256> resultBuilder2;
                    resultBuilder2.appendFormat( "first divergent tick %# (%# common tick(s))", comparison._firstDivergentTick, comparison._commonTickCount );
                    return string( resultBuilder2.view() );
                }
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "no divergence over %# common tick(s)", comparison._commonTickCount );
                return string( resultBuilder.view() );
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
                if ( const expected<void> loaded = loadSessionServer( 0 ); loaded.has_value() == false )
                {
                    outReply = makeReply( loaded.error() );
                    return true;
                }
                if ( bServer )
                {
                    const expected<void> loaded = loadSessionServer( 1 );
                    if ( loaded.has_value() == false )
                        outReply = makeReply( loaded.error() );
                    bDone = loaded.has_value() && compare.compareObjectsFromChannels();
                }
                else
                {
                    EditorNetPlaySession* pSession   = MultiplayerSessionViewModel::findSession();
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
                        if ( pSession->requestObjectHashes( 1, serverTick ) == false )
                        {
                            outReply = "netplay sync objects client could not ask the client (its process is gone)";
                            return true;
                        }
                        StringBuilder<constant::buffer::kMax256> reply;
                        reply.appendFormat( "asked %# for its object hashes at tick %# - wait until Editor.NetPlayObjectHashReports changes, then run this again", status._label, serverTick );
                        outReply = string( reply.view() );
                        return true;
                    }
                    bDone = compare.compareObjects( serverTick, listServer, status._objectHashTick, status._listObjectHash );
                }
                if ( bDone )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "netplay sync objects tick %#: %# differing object(s)", compare.getObjectTick(), compare.getDifferingObjects().size() );
                    outReply = string( replyBuilder.view() );
                }
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
                if ( const expected<void> loaded = loadSessionServer( 0 ); loaded.has_value() == false )
                {
                    outReply = makeReply( loaded.error() );
                    return true;
                }
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

    SW_AUTOMATION_WATCH( editorNetPlaySyncCommonTicks, "Editor.NetPlaySyncCommonTicks", "Ticks both state hash histories of the multiplayer Sync tab held at the last comparison",
                         &MultiplayerSyncViewModelInternal::readCommonTicks );
    SW_AUTOMATION_WATCH( editorNetPlaySyncDiverged, "Editor.NetPlaySyncDiverged", "1 when the multiplayer Sync tab's last comparison found a divergent tick",
                         &MultiplayerSyncViewModelInternal::readDiverged );
    SW_AUTOMATION_WATCH( editorNetPlaySyncObjectsCompared, "Editor.NetPlaySyncObjectsCompared", "1 when the Sync tab's last 'Which objects differ' compared the two object hash lists",
                         &MultiplayerSyncViewModelInternal::readObjectsCompared );
    SW_AUTOMATION_WATCH( editorNetPlaySyncDifferingObjects, "Editor.NetPlaySyncDifferingObjects", "Objects the Sync tab's last 'Which objects differ' found different",
                         &MultiplayerSyncViewModelInternal::readDifferingObjects );
    SW_DEV_COMMAND( NetPlaySync, "netplay.sync", "netplay.sync save|compare <path> | netplay.sync objects server|client | netplay.sync channel <name>",
                    "Save the session server's per-tick state hash history to a file, or compare it with a saved one (first divergent tick), or list the objects that differ (Which objects differ), or pick the hash line to read (scene, or a kit's such as destruction · rollback)",
                    &MultiplayerSyncViewModelInternal::runSync );
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerSyncViewModel::MultiplayerSyncViewModel()
        : _pModel{ make_unique<MultiplayerSyncModel>() }
    {
    }

    MultiplayerSyncViewModel::~MultiplayerSyncViewModel() = default;

    void MultiplayerSyncViewModel::poll()
    {
        _pModel->poll( MultiplayerSessionViewModel::getSession().getNetPlayProcesses(), MultiplayerSyncViewModelInternal::getCompare(),
                       MultiplayerSyncViewModelInternal::getReplyTimeoutMilliseconds() );
    }

    void MultiplayerSyncViewModel::gather( MultiplayerSyncViewState& outState ) const
    {
        const EditorStateHashCompare& compare = MultiplayerSyncViewModelInternal::getCompare();
        for ( int32 sourceIndex = 0; sourceIndex < kSourceCount; ++sourceIndex )
        {
            const EditorStateHashSource& source = compare.getSource( sourceIndex );
            MultiplayerSyncSourceRow&    row    = outState._arrSource[sourceIndex];
            row._label                          = source._label;
            row._sampleCount                    = static_cast<uint32>( source._listSample.size() );
            row._firstTick                      = source._listSample.empty() ? 0 : source._listSample.front()._tick;
            row._lastTick                       = source._listSample.empty() ? 0 : source._listSample.back()._tick;
        }
        // 0 번은 서버다. 클라이언트는 이력을 디버그 채널이 아니라 표준 입력 명령의 답(로그 한 줄)으로 낸다.
        const EditorNetPlaySession& session = MultiplayerSessionViewModel::getSession();
        outState._bSessionRunning           = session.isRunning() ? SW_TRUE : SW_FALSE;
        const int32 clientCount             = session.isRunning() && session.getProcessCount() > 1 ? session.getProcessCount() - 1 : 0;
        outState._listClient.resize( static_cast<size_t>( clientCount ) );
        for ( int32 clientIndex = 0; clientIndex < clientCount; ++clientIndex )
        {
            const int32                      processIndex = clientIndex + 1;
            const EditorNetPlayProcessStatus status       = session.getProcessStatus( processIndex );
            MultiplayerSyncClientRow&        row          = outState._listClient[static_cast<size_t>( clientIndex )];
            row._label                                    = status._label;
            row._processIndex                             = processIndex;
            row._bRunning                                 = status._bRunning ? SW_TRUE : SW_FALSE;
            for ( int32 sourceIndex = 0; sourceIndex < kSourceCount; ++sourceIndex )
            {
                row._arrWaiting[sourceIndex] = _pModel->isWaitingForHistory( sourceIndex, processIndex ) ? SW_TRUE : SW_FALSE;
            }
        }
        outState._listDifferingObject = compare.getDifferingObjects();
        outState._hashChannel         = compare.getHashChannel();
        outState._comparisonText      = compare.isComparisonCurrent() ? MultiplayerSyncViewModelInternal::describeComparison( compare.getComparison() ) : string{};
        outState._error               = _pModel->getError();
        outState._objectTick          = compare.getObjectTick();
        outState._bObjectComparison   = compare.hasObjectComparison() ? SW_TRUE : SW_FALSE;
    }

    void MultiplayerSyncViewModel::setHashChannel( string_view channelName )
    {
        EditorStateHashCompare& compare = MultiplayerSyncViewModelInternal::getCompare();
        if ( compare.setHashChannel( channelName ) == false )
            _pModel->setError( compare.getLastError() );
        else
            _pModel->clearError();
    }

    void MultiplayerSyncViewModel::loadSessionServer( int32 sourceIndex )
    {
        const expected<void> loaded = MultiplayerSyncViewModelInternal::loadSessionServer( sourceIndex );
        if ( loaded.has_value() == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotRead", "Sync could not read the session server's history: %#: %#", loaded.error().message(), loaded.error().detail() );
    }

    void MultiplayerSyncViewModel::requestClientHistory( int32 sourceIndex, int32 processIndex )
    {
        (void)_pModel->requestClientHistory( MultiplayerSessionViewModel::getSession().getNetPlayProcesses(), MultiplayerSyncViewModelInternal::getCompare(), sourceIndex,
                                             processIndex ); // 실패는 모델이 알린다
    }

    void MultiplayerSyncViewModel::readServer( int32 sourceIndex, string_view address, string_view token )
    {
        EditorStateHashCompare& compare = MultiplayerSyncViewModelInternal::getCompare();
        NetAddress              remote;
        const bool              bLoopback = NetAddress::parse( address, 0, remote ) && remote.isValid() && remote.isLoopback();
        if ( bLoopback == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncReadsOnlyPort", "Sync reads only 127.0.0.1:<port> - '%#' is not one", string( address ) );
        else if ( compare.loadFromChannel( sourceIndex, remote, token ) == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotRead2", "Sync could not read %#: %#", remote.toString(), compare.getLastError() );
    }

    void MultiplayerSyncViewModel::loadFile( int32 sourceIndex, string_view typedPath )
    {
        EditorStateHashCompare& compare = MultiplayerSyncViewModelInternal::getCompare();
        if ( compare.loadFromFile( sourceIndex, MultiplayerSyncViewModelInternal::resolveTypedPath( typedPath ) ) == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotLoad", "Sync could not load %#: %#", string( typedPath ), compare.getLastError() );
    }

    void MultiplayerSyncViewModel::saveFile( int32 sourceIndex, string_view typedPath )
    {
        if ( MultiplayerSyncViewModelInternal::getCompare().saveToFile( sourceIndex, MultiplayerSyncViewModelInternal::resolveTypedPath( typedPath ) ) == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotWrite", "Sync could not write %#", string( typedPath ) );
    }

    void MultiplayerSyncViewModel::clearSource( int32 sourceIndex )
    {
        MultiplayerSyncViewModelInternal::getCompare().clearSource( sourceIndex );
    }

    void MultiplayerSyncViewModel::compare()
    {
        EditorStateHashCompare& compare = MultiplayerSyncViewModelInternal::getCompare();
        if ( compare.compare() == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotCompare2", "Sync could not compare: %#", compare.getLastError() );
    }

    void MultiplayerSyncViewModel::startObjectCompare()
    {
        _pModel->startObjectCompare( MultiplayerSessionViewModel::getSession().getNetPlayProcesses(), MultiplayerSyncViewModelInternal::getCompare(),
                                     MultiplayerSyncViewModelInternal::getReplyTimeoutMilliseconds() );
    }

    void MultiplayerSyncViewModel::startReplayServer( const string& intentPath )
    {
        if ( MultiplayerSessionViewModel::getSession().startReplayServer( intentPath ) == false )
            _pModel->setError( "The replay server could not start - see the Output Log" );
    }

    void MultiplayerSyncViewModel::stepServer( int32 tickCount )
    {
        fixed_string<constant::buffer::kMax32> command;
        formatstring( command.data(), command.capacity(), "step %#", tickCount );
        if ( MultiplayerSessionViewModel::getSession().sendServerCommand( command.view() ) == false )
            _pModel->setError( "The server did not take the step command" );
    }
} // namespace sw::editor
