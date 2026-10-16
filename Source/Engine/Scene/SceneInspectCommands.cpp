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
                const SerializeContext& context = SerializeContext::getDefault();
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
                        outReply += SerializerUtil::formatPropertyText( prop, pComponent, context );
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

            static bool runStateHashes( const vector<string>& listArgument, string& outReply )
            {
                uint64     fromTick  = 0;
                uint64     count     = 0;
                const bool bHasFrom  = listArgument.size() == 2 && StringUtil::parseUint64( listArgument[0], fromTick );
                const bool bHasCount = bHasFrom && StringUtil::parseUint64( listArgument[1], count );
                if ( bHasCount == false )
                    return false;
                if ( StateHashHistory::get().isRecording() == false )
                {
                    outReply = "state hash history is off - start with -gv_stateHashHistory=<ticks>";
                    return false;
                }
                vector<StateHashSample> listSample;
                StateHashHistory::get().collect( fromTick, static_cast<uint32>( count ), listSample );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SceneTree, "scene.tree", "scene.tree [limit]", "List the objects of the active scene (id, name, parent, active, components)", &SceneInspectCommandsInternal::runTree );
    SW_DEV_COMMAND( ObjectDump, "object.dump", "object.dump <name>", "Print the components and property values of a named object of the active scene",
                    &SceneInspectCommandsInternal::runObjectDump );
    SW_DEV_COMMAND( ScenePoses, "scene.poses", "scene.poses", "Print the world position of every named object that has a transform (name x y z)", &SceneInspectCommandsInternal::runPoses );
    SW_DEV_COMMAND( StateHash, "state.hash", "state.hash", "Print the state hash of the active scene right now", &SceneInspectCommandsInternal::runStateHash );
    SW_DEV_COMMAND( StateHashes, "state.hashes", "state.hashes <fromTick> <count>", "Print the recorded per-tick state hashes (needs -gv_stateHashHistory)",
                    &SceneInspectCommandsInternal::runStateHashes );
} // namespace sw

#endif
