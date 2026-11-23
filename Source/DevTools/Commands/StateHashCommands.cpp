#include "pch.h"

#include "Core/Foundation/Expected.h"
#include "Core/Network/DebugReplyLine.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectHash.h"

namespace sw
{
    namespace
    {
        /** @brief 상태 해시 개발 명령(`state.*`)입니다. */
        struct StateHashCommandsInternal
        {
            /** @brief 기록 중인 이력 서비스입니다. 없거나 꺼져 있으면 그 까닭(명령 답이 문장 그대로다)입니다. */
            [[nodiscard]] static expected<const StateHashHistory*> findRecordingHistory()
            {
                const StateHashHistory* pHistory = engine::findSubsystem<StateHashHistory>();
                if ( pHistory == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "state hash history service is not running in this host" } );
                if ( pHistory->isRecording() == false )
                    return unexpected( Error{ ErrorKind::Unsupported, "state hash history is off - start with -gv_stateHashHistory=<ticks>" } );
                return pHistory;
            }

            static bool runStateHash( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                vector<StateHashSample> listSample;
                listSample.push_back( StateHashSample{ 0, SceneObjectHash::compute( key, *pManager ) } );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }

            /** @brief `state.objects` — 이름 있는 오브젝트마다 해시(`<16자리 16진 해시> <이름>`)를 답합니다. 어긋난 틱 근처에서 두 프로세스의 답을 견주면 어느 오브젝트가 다른지 나옵니다(`SceneObjectHash::findDifferingObjects`). */
            static bool runStateObjects( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<SceneObjectHash> listObject;
                const expected<uint64>  tick = collectTickedObjects( key, listObject );
                if ( tick.has_value() == false )
                {
                    outReply = string( tick.error().message() );
                    return false;
                }
                SceneObjectHash::writeTickedObjectReport( *tick, listObject, outReply );
                return true;
            }

            /** @brief `state.objectline` — `state.objects` 와 같은 값(틱 + 오브젝트별 해시)을 한 줄(`SceneObjectHash::writeObjectReportLine`)로 답합니다. 표준 입력으로 묻는 에디터 Sync 탭의 클라이언트 칸이 쓴다. */
            static bool runStateObjectLine( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<SceneObjectHash> listObject;
                const expected<uint64>  tick = collectTickedObjects( key, listObject );
                if ( tick.has_value() == false )
                {
                    outReply = string( tick.error().message() );
                    return false;
                }
                if ( SceneObjectHash::writeObjectReportLine( *tick, listObject, outReply ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> reply;
                    reply.appendFormat( "the object report does not fit one line (%# objects, limit %# bytes) - use state.objects", listObject.size(), DebugReplyLine::kMaxBytes );
                    outReply = string( reply.view() );
                    return false;
                }
                return true;
            }

            /**
             * @brief 두 `state.objects*` 명령이 같이 쓰는 값입니다. 돌려주는 틱은 이력의 가장 최근 틱이다 — 이력이 없으면 어느 틱의 해시인지 말할 수 없어
             *        답하지 않는다(그 까닭 — 문장이 명령 답이다).
             */
            [[nodiscard]] static expected<uint64> collectTickedObjects( SceneIterationKey key, vector<SceneObjectHash>& outListObject )
            {
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                    return unexpected( history.error() );
                StateHashSample latest;
                if ( ( *history )->tryGetLatest( latest ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "state hash history has no tick yet" } );
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "no active scene" } );
                SceneObjectHash::collectObjects( key, *pManager, outListObject );
                return latest._tick;
            }

            /** @brief 이력의 줄 이름 인자입니다(생략하면 씬 줄). 그 줄이 있는지는 부르는 쪽이 `StateHashHistory::hasChannel` 로 본다. */
            [[nodiscard]] static string_view readChannelName( const vector<string>& listArgument, size_t nameIndex )
            {
                return listArgument.size() > nameIndex ? string_view{ listArgument[nameIndex] } : string_view{ StateHashHistory::kSceneChannelName };
            }

            /** @brief 그 이름의 줄이 없을 때의 답입니다. */
            [[nodiscard]] static string makeNoChannelReply( string_view channelName )
            {
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "no state hash channel '%#' - state.hashnames lists them", channelName );
                return string( reply.view() );
            }

            static bool runStateHashes( const vector<string>& listArgument, string& outReply )
            {
                uint64     fromTick  = 0;
                uint64     count     = 0;
                const bool bArgCount = listArgument.size() == 2 || listArgument.size() == 3;
                const bool bHasFrom  = bArgCount && StringUtil::parseUint64( listArgument[0], fromTick );
                const bool bHasCount = bHasFrom && StringUtil::parseUint64( listArgument[1], count );
                if ( bHasCount == false )
                    return false;
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                {
                    outReply = string( history.error().message() );
                    return false;
                }
                const StateHashHistory* pHistory    = *history;
                const string_view       channelName = readChannelName( listArgument, 2 );
                if ( pHistory->hasChannel( channelName ) == false )
                {
                    outReply = makeNoChannelReply( channelName );
                    return false;
                }
                vector<StateHashSample> listSample;
                pHistory->collectChannel( channelName, fromTick, static_cast<uint32>( count ), listSample );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }

            /** @brief `state.hashnames` — 해시 이력의 줄 이름(씬 줄 `scene` 과 키트가 더한 줄)을 한 줄에 하나씩 답합니다. */
            static bool runStateHashNames( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                {
                    outReply = string( history.error().message() );
                    return false;
                }
                const StateHashHistory* pHistory = *history;
                vector<string>          listName;
                pHistory->collectChannelNames( listName );
                outReply = StateHashHistory::kSceneChannelName;
                outReply += '\n';
                for ( const string& name : listName )
                {
                    outReply += name;
                    outReply += '\n';
                }
                return true;
            }

            /** @brief `state.hashline <count> [name]` — 가장 최근 @p count 틱의 이력(@p name 은 키트의 줄, 생략하면 씬 줄)을 한 줄(`StateHashHistory::writeReportLine`)로 답합니다. 표준 입력으로 묻는 에디터 Sync 탭이 쓴다. */
            static bool runStateHashLine( const vector<string>& listArgument, string& outReply )
            {
                uint64     count     = 0;
                const bool bHasCount = ( listArgument.size() == 1 || listArgument.size() == 2 ) && StringUtil::parseUint64( listArgument[0], count );
                if ( bHasCount == false || count == 0 )
                    return false;
                if ( count > StateHashHistory::kMaxReportLineSamples )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "count %# does not fit one line (at most %#) - use state.hashes", count, StateHashHistory::kMaxReportLineSamples );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                {
                    outReply = string( history.error().message() );
                    return false;
                }
                const StateHashHistory* pHistory    = *history;
                const string_view       channelName = readChannelName( listArgument, 1 );
                if ( pHistory->hasChannel( channelName ) == false )
                {
                    outReply = makeNoChannelReply( channelName );
                    return false;
                }
                vector<StateHashSample> listSample;
                StateHashSample         latest;
                if ( pHistory->tryGetLatestChannel( channelName, latest ) )
                {
                    const uint64 fromTick = latest._tick + 1 > count ? latest._tick + 1 - count : 0;
                    pHistory->collectChannel( channelName, fromTick, static_cast<uint32>( count ), listSample );
                }
                if ( StateHashHistory::writeReportLine( listSample, outReply ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "the hash history does not fit one line (limit %# bytes) - use state.hashes", DebugReplyLine::kMaxBytes );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                return true;
            }
        };
    } // namespace

    SW_DEV_SCENE_COMMAND( StateHash, "state.hash", "state.hash", "Print the state hash of the active scene right now", &StateHashCommandsInternal::runStateHash );
    SW_DEV_SCENE_COMMAND( StateObjects, "state.objects", "state.objects", "Print one hash per named object of the active scene (compare two processes to find which object differs)",
                          &StateHashCommandsInternal::runStateObjects );
    SW_DEV_COMMAND( StateHashes, "state.hashes", "state.hashes <fromTick> <count> [channel]",
                    "Print the recorded per-tick state hashes (needs -gv_stateHashHistory); channel is a kit's hash line from state.hashnames, default the scene",
                    &StateHashCommandsInternal::runStateHashes );
    SW_DEV_COMMAND( StateHashNames, "state.hashnames", "state.hashnames", "List the state hash history's channels (the scene and the lines kits registered, e.g. destruction, rollback)",
                    &StateHashCommandsInternal::runStateHashNames );
    SW_DEV_COMMAND( StateHashLine, "state.hashline", "state.hashline <count> [channel]",
                    "Print the latest <count> recorded state hashes (of a kit's hash line, default the scene) on one line (the editor Sync tab reads client histories this way)",
                    &StateHashCommandsInternal::runStateHashLine );
    SW_DEV_SCENE_COMMAND( StateObjectLine, "state.objectline", "state.objectline", "Print the state.objects hashes with their tick on one line (the editor Sync tab reads client object hashes this way)",
                          &StateHashCommandsInternal::runStateObjectLine );
} // namespace sw
