/**
 * @file SceneInspectCommands.cpp
 * @brief 활성 씬을 글로 읽는 개발 명령입니다(서버 검사기 S3 · 권위 겹쳐 보기 S6 · 어긋남 찾기 S7). Shipping 에는 없습니다.
 * @details 읽기 전용입니다. 값을 바꾸는 일은 기존 개발 명령(`teleport` · `tag.add`)처럼 게임이 정한 경로를 거칩니다(결정 기록 5-10).
 *          디버그 채널(`DebugChannel`)은 이 명령들을 원격으로 부를 뿐이라, 같은 명령을 표준 입력 콘솔과 에디터 콘솔에서도 씁니다.
 *          - `scene.tree [limit]`: 오브젝트 한 줄씩(`id name parent=… active=… components=…`), ID 순.
 *          - `object.dump <name>`: 이름이 같은 첫 오브젝트의 컴포넌트와 프로퍼티 값(`Type.property = value`).
 *          - `scene.poses`: 이름 있는 오브젝트의 월드 위치(`name x y z`), 이름 순. 클라이언트의 권위 고스트(`AuthorityGhost`)가 읽는다.
 *          - `state.hash`: 지금 씬 상태 해시. `state.hashes <from> <count>`: 틱별 이력(`SceneStateHash`).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"
    #include "Core/Network/DebugReplyLine.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Object/Component/Component.h"
    #include "Engine/Object/Component/SceneComponent.h"
    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Reflection/ReflectionTypes.h"
    #include "Engine/Scene/AuthorityGhost.h"
    #include "Engine/Scene/SceneManager.h"
    #include "Engine/Scene/SceneStateHash.h"
    #include "Engine/Serialization/Base/SerializerUtil.h"

    #include <algorithm>
    #include <charconv>

namespace sw
{
    namespace
    {
        struct SceneInspectCommandsInternal
        {
            static constexpr uint32 kDefaultTreeLimit = 200;

            static void appendNumber( string& outText, uint64 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            static void appendFloat( string& outText, float32 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            /** @brief 이력을 남기고 있는 상태 해시 이력입니다. 서비스가 없거나 꺼져 있으면 nullptr 이고 @p outReply 에 까닭을 씁니다. */
            static const StateHashHistory* findRecordingHistory( string& outReply )
            {
                const StateHashHistory* pHistory = engine::findSubsystem<StateHashHistory>();
                if ( pHistory == nullptr )
                    outReply = "state hash history service is not running in this host";
                else if ( pHistory->isRecording() == false )
                    outReply = "state hash history is off - start with -gv_stateHashHistory=<ticks>";
                else
                    return pHistory;
                return nullptr;
            }

            static bool runTree( const vector<string>& listArgument, string& outReply )
            {
                uint64 limit = kDefaultTreeLimit;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && StringUtil::parseUint64( listArgument[0], limit ) == false ) )
                    return false;
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                vector<GameObject*> listObject;
                pManager->getAllGameObjects( listObject );
                std::sort( listObject.begin(), listObject.end(), []( const GameObject* pLeft, const GameObject* pRight )
                {
                    return pLeft->getObjectID() < pRight->getObjectID();
                } );
                outReply = "objects ";
                appendNumber( outReply, listObject.size() );
                outReply += '\n';
                uint64 written = 0;
                for ( const GameObject* pObject : listObject )
                {
                    if ( written++ >= limit )
                    {
                        outReply += "... (limit reached)\n";
                        break;
                    }
                    appendNumber( outReply, pObject->getObjectID() );
                    outReply += ' ';
                    outReply += pObject->getName().empty() ? "<unnamed>" : pObject->getName().c_str();
                    const GameObject* pParent = pObject->getParent();
                    outReply += " parent=";
                    outReply += pParent != nullptr && pParent->getName().empty() == false ? pParent->getName().c_str() : "-";
                    outReply += pObject->isActive() ? " active=1" : " active=0";
                    outReply += " components=";
                    bool bFirst = true;
                    pObject->forEachComponent( [&outReply, &bFirst]( Component* pComponent )
                    {
                        const TypeInfo* pType = pComponent->getTypeInfo();
                        if ( bFirst == false )
                            outReply += ',';
                        bFirst = false;
                        outReply += pType != nullptr ? pType->_name.c_str() : "?";
                    } );
                    outReply += '\n';
                }
                return true;
            }

            static bool runObjectDump( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                if ( pObject == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' in the active scene";
                    return false;
                }
                outReply = "object ";
                outReply += listArgument[0];
                outReply += " id=";
                appendNumber( outReply, pObject->getObjectID() );
                outReply += pObject->isActive() ? " active=1\n" : " active=0\n";
                const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Snapshot );
                pObject->forEachComponent( [&outReply, &context]( Component* pComponent )
                {
                    const TypeInfo* pType = pComponent->getTypeInfo();
                    if ( pType == nullptr )
                        return;
                    outReply += '[';
                    outReply += pType->_name.c_str();
                    outReply += "]\n";
                    pType->forEachProperty( [&outReply, &context, pComponent]( const PropertyInfo& prop )
                    {
                        outReply += "  ";
                        outReply += prop._name.c_str();
                        outReply += " = ";
                        outReply += SerializerUtil::formatPropertyText( prop, ObjectAddress{ pComponent }, context );
                        outReply += '\n';
                    }, true );
                } );
                return true;
            }

            static bool runPoses( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                vector<AuthorityPose> listPose;
                AuthorityGhost::collectPoses( *pManager, listPose );
                outReply.clear();
                for ( const AuthorityPose& pose : listPose )
                {
                    outReply += pose._name;
                    outReply += ' ';
                    appendFloat( outReply, pose._position._x );
                    outReply += ' ';
                    appendFloat( outReply, pose._position._y );
                    outReply += ' ';
                    appendFloat( outReply, pose._position._z );
                    outReply += '\n';
                }
                return true;
            }

            static bool runStateHash( const vector<string>& listArgument, string& outReply )
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
                listSample.push_back( StateHashSample{ 0, SceneStateHash::compute( *pManager ) } );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }

            /** @brief `state.objects` — 이름 있는 오브젝트마다 해시(`<16자리 16진 해시> <이름>`)를 답합니다. 어긋난 틱 근처에서 두 프로세스의 답을 견주면 어느 오브젝트가 다른지 나옵니다(`SceneStateHash::findDifferingObjects`). */
            static bool runStateObjects( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                uint64                  tick = 0;
                vector<SceneObjectHash> listObject;
                if ( collectTickedObjects( outReply, tick, listObject ) == false )
                    return false;
                SceneStateHash::writeTickedObjectReport( tick, listObject, outReply );
                return true;
            }

            /** @brief `state.objectline` — `state.objects` 와 같은 값(틱 + 오브젝트별 해시)을 한 줄(`SceneStateHash::writeObjectReportLine`)로 답합니다. 표준 입력으로 묻는 에디터 Sync 탭의 클라이언트 칸이 쓴다. */
            static bool runStateObjectLine( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                uint64                  tick = 0;
                vector<SceneObjectHash> listObject;
                if ( collectTickedObjects( outReply, tick, listObject ) == false )
                    return false;
                if ( SceneStateHash::writeObjectReportLine( tick, listObject, outReply ) == false )
                {
                    outReply = "the object report does not fit one line (" + to_string( listObject.size() ) + " objects, limit " + to_string( DebugReplyLine::kMaxBytes ) +
                               " bytes) - use state.objects";
                    return false;
                }
                return true;
            }

            /** @brief 두 `state.objects*` 명령이 같이 쓰는 값입니다. 첫 값의 틱은 이력의 가장 최근 틱이다 — 이력이 없으면 어느 틱의 해시인지 말할 수 없어 답하지 않는다(이유는 @p outReply). */
            [[nodiscard]] static bool collectTickedObjects( string& outReply, uint64& outTick, vector<SceneObjectHash>& outListObject )
            {
                const StateHashHistory* pHistory = findRecordingHistory( outReply );
                if ( pHistory == nullptr )
                    return false;
                StateHashSample latest;
                if ( pHistory->tryGetLatest( latest ) == false )
                {
                    outReply = "state hash history has no tick yet";
                    return false;
                }
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                outTick = latest._tick;
                SceneStateHash::collectObjects( *pManager, outListObject );
                return true;
            }

            static bool runStateHashes( const vector<string>& listArgument, string& outReply )
            {
                uint64     fromTick  = 0;
                uint64     count     = 0;
                const bool bHasFrom  = listArgument.size() == 2 && StringUtil::parseUint64( listArgument[0], fromTick );
                const bool bHasCount = bHasFrom && StringUtil::parseUint64( listArgument[1], count );
                if ( bHasCount == false )
                    return false;
                const StateHashHistory* pHistory = findRecordingHistory( outReply );
                if ( pHistory == nullptr )
                    return false;
                vector<StateHashSample> listSample;
                pHistory->collect( fromTick, static_cast<uint32>( count ), listSample );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }

            /** @brief `state.hashline <count>` — 가장 최근 @p count 틱의 이력을 한 줄(`StateHashHistory::writeReportLine`)로 답합니다. 표준 입력으로 묻는 에디터 Sync 탭이 쓴다. */
            static bool runStateHashLine( const vector<string>& listArgument, string& outReply )
            {
                uint64     count     = 0;
                const bool bHasCount = listArgument.size() == 1 && StringUtil::parseUint64( listArgument[0], count );
                if ( bHasCount == false || count == 0 )
                    return false;
                if ( count > StateHashHistory::kMaxReportLineSamples )
                {
                    outReply = "count " + to_string( count ) + " does not fit one line (at most " + to_string( StateHashHistory::kMaxReportLineSamples ) + ") - use state.hashes";
                    return false;
                }
                const StateHashHistory* pHistory = findRecordingHistory( outReply );
                if ( pHistory == nullptr )
                    return false;
                vector<StateHashSample> listSample;
                StateHashSample         latest;
                if ( pHistory->tryGetLatest( latest ) )
                {
                    const uint64 fromTick = latest._tick + 1 > count ? latest._tick + 1 - count : 0;
                    pHistory->collect( fromTick, static_cast<uint32>( count ), listSample );
                }
                if ( StateHashHistory::writeReportLine( listSample, outReply ) == false )
                {
                    outReply = "the hash history does not fit one line (limit " + to_string( DebugReplyLine::kMaxBytes ) + " bytes) - use state.hashes";
                    return false;
                }
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SceneTree, "scene.tree", "scene.tree [limit]", "List the objects of the active scene (id, name, parent, active, components)", &SceneInspectCommandsInternal::runTree );
    SW_DEV_COMMAND( ObjectDump, "object.dump", "object.dump <name>", "Print the components and property values of a named object of the active scene",
                    &SceneInspectCommandsInternal::runObjectDump );
    SW_DEV_COMMAND( ScenePoses, "scene.poses", "scene.poses", "Print the world position of every named object that has a transform (name x y z)", &SceneInspectCommandsInternal::runPoses );
    SW_DEV_COMMAND( StateHash, "state.hash", "state.hash", "Print the state hash of the active scene right now", &SceneInspectCommandsInternal::runStateHash );
    SW_DEV_COMMAND( StateObjects, "state.objects", "state.objects", "Print one hash per named object of the active scene (compare two processes to find which object differs)",
                    &SceneInspectCommandsInternal::runStateObjects );
    SW_DEV_COMMAND( StateHashes, "state.hashes", "state.hashes <fromTick> <count>", "Print the recorded per-tick state hashes (needs -gv_stateHashHistory)",
                    &SceneInspectCommandsInternal::runStateHashes );
    SW_DEV_COMMAND( StateHashLine, "state.hashline", "state.hashline <count>", "Print the latest <count> recorded state hashes on one line (the editor Sync tab reads client histories this way)",
                    &SceneInspectCommandsInternal::runStateHashLine );
    SW_DEV_COMMAND( StateObjectLine, "state.objectline", "state.objectline", "Print the state.objects hashes with their tick on one line (the editor Sync tab reads client object hashes this way)",
                    &SceneInspectCommandsInternal::runStateObjectLine );
} // namespace sw

#endif
