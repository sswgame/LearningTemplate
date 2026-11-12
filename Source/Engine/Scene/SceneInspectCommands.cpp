#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include <charconv>

/**
 * @file SceneInspectCommands.cpp
 * @brief 활성 씬을 들여다보고 고치는 개발 명령입니다(`scene.tree` · `object.dump` · `object.place`). 상태 해시 명령(`state.*`)은 `SceneObjectHash.cpp` 에 있다.
 */
namespace sw
{
    namespace
    {
        struct SceneInspectCommandsInternal
        {
            static constexpr uint32 kDefaultTreeLimit = 200;

            static bool runTree( SceneWalkKey key, const vector<string>& listArgument, string& outReply )
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
                pManager->getAllGameObjects( key, listObject );
                VectorUtil::sort( listObject, []( const GameObject* pLeft, const GameObject* pRight )
                {
                    return pLeft->getObjectID() < pRight->getObjectID();
                } );
                outReply = "objects ";
                StringUtil::appendNumber( outReply, listObject.size() );
                outReply += '\n';
                uint64 written = 0;
                for ( const GameObject* pObject : listObject )
                {
                    if ( written++ >= limit )
                    {
                        outReply += "... (limit reached)\n";
                        break;
                    }
                    StringUtil::appendNumber( outReply, pObject->getObjectID() );
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
                StringUtil::appendNumber( outReply, pObject->getObjectID() );
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

            /** @brief 이름 붙은 오브젝트의 주 씬 컴포넌트를 로컬 위치 (x, y, z) 에 둡니다(시나리오가 카메라를 멀리 두어 LOD 를 바꾸는 단계). */
            static bool runObjectPlace( const vector<string>& listArgument, string& outReply )
            {
                float3     position{};
                const bool bParsed = listArgument.size() == 4 && StringUtil::parseFloat( listArgument[1], position._x ) && StringUtil::parseFloat( listArgument[2], position._y ) &&
                                     StringUtil::parseFloat( listArgument[3], position._z );
                if ( bParsed == false )
                    return false;
                GameObject*     pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
                if ( pScene == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' with a scene component in the active scene";
                    return false;
                }
                pScene->setLocalPosition( position );
                outReply = "placed " + listArgument[0];
                return true;
            }
        };
    } // namespace

    SW_DEV_SCENE_COMMAND( SceneTree, "scene.tree", "scene.tree [limit]", "List the objects of the active scene (id, name, parent, active, components)", &SceneInspectCommandsInternal::runTree );
    SW_DEV_COMMAND( ObjectDump, "object.dump", "object.dump <name>", "Print the components and property values of a named object of the active scene",
                    &SceneInspectCommandsInternal::runObjectDump );
    SW_DEV_COMMAND( ObjectPlace, "object.place", "object.place <name> <x> <y> <z>", "Put a named object of the active scene at a local position (scenarios move the camera to change LOD)",
                    &SceneInspectCommandsInternal::runObjectPlace );
} // namespace sw
