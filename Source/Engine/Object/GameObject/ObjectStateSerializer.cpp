#include "pch.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Core/BinaryStream.h"
#include "Engine/Serialization/Core/Serializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    namespace
    {
        struct ObjectStateSerializerInternal
        {
            /** @brief 이름으로 컴포넌트를 만들어 소유자에 붙입니다 (역직렬화 팩토리). */
            static void* createOwnedComponent( void* pOuter, hashed_string typeName )
            {
                GameObject* pGameObject = static_cast<GameObject*>( pOuter );
                if ( pGameObject == nullptr || pGameObject->getManager() == nullptr )
                    return nullptr;
                return pGameObject->getManager()->addComponentByName( pGameObject, typeName, false );
            }

            static const TypeInfo* getComponentRuntimeTypeInfo( const void* pInstance )
            {
                const Component* pComp = static_cast<const Component*>( pInstance );
                if ( pComp == nullptr || pComp->isPendingKill() )
                    return nullptr;
                return pComp->getTypeInfo();
            }

            static SerializeContext makeGameObjectXmlContext( GameObject* pGameObject )
            {
                SerializeContext ctx = SerializeContext::deriveFromDefault();
                ctx.setOuterInstance( pGameObject );
                ctx.setOwnedPointerFactory( &createOwnedComponent );
                ctx.setRuntimeTypeInfoFn( &getComponentRuntimeTypeInfo );
                return ctx;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ObjectStateSerializer::saveToXmlString( const GameObject* pGameObject )
    {
        if ( pGameObject == nullptr )
            return {};

        pGameObject->prepareSerialize();

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return {};

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
        return XmlSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
    }

    string ObjectStateSerializer::saveToJsonString( const GameObject* pGameObject )
    {
        if ( pGameObject == nullptr )
            return {};

        pGameObject->prepareSerialize();

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return {};

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
        return JsonSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
    }

    bool ObjectStateSerializer::saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer )
    {
        if ( pGameObject == nullptr )
            return false;

        pGameObject->prepareSerialize();

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        BinaryStreamWriter writer( outBuffer );

        // **바깥에 남는 것은 부모 이름 하나뿐이다.** 오브젝트 사이의 부모 관계만 리플렉션 상태에
        // 없고(씬이 나중에 rebind 한다), 이름·활성·태그·컴포넌트·컴포넌트 간 부착은 전부
        // `_name` / `_bActive` / `_listComponent` 로 실린다 — XML·JSON 과 같은 상태다.
        string parentName;
        if ( pGameObject->getParent() != nullptr )
            parentName = pGameObject->getParent()->getName().c_str();
        writer.writeString( parentName );

        // 본문 크기를 앞에 둔다 — 세이브게임은 오브젝트를 이어 붙여 놓고 하나씩 끊어 읽는다.
        const size_t sizeHeaderPos = writer.getOffset();
        writer.write( static_cast<uint32>( 0 ) );

        const size_t     bodyStart = writer.getOffset();
        SerializeContext ctx       = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
        BinarySerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, outBuffer, ctx );
        writer.writeAt( sizeHeaderPos, static_cast<uint32>( writer.getOffset() - bodyStart ) );

        return true;
    }

    size_t ObjectStateSerializer::loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, string& outParentName )
    {
        if ( pGameObject == nullptr || pData == nullptr || size == 0 )
            return 0;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return 0;

        BinaryStreamReader reader( pData, size );
        if ( reader.readString( outParentName ) == false )
            return 0;

        uint32 bodySize{ 0 };
        if ( reader.read( bodySize ) == false )
            return 0;

        const size_t bodyStart = reader.getOffset();
        if ( bodyStart + bodySize > size )
            return 0;

        const hashed_string oldName = pGameObject->getName();
        pGameObject->clearComponents();

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32           ver{ 0 };
        if ( BinarySerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, pData + bodyStart, bodySize,
                                                     kObjectReflectedSchemaVersion, nullptr, nullptr, ctx ) == false )
            return 0;

        if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
            pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

        pGameObject->setActive( pGameObject->isActive() );
        pGameObject->applyLoadedHierarchy();

        return bodyStart + bodySize;
    }

    bool ObjectStateSerializer::loadFromXmlString( GameObject* pGameObject, string_view xmlString )
    {
        if ( pGameObject == nullptr || xmlString.empty() )
            return false;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        const hashed_string oldName = pGameObject->getName();
        pGameObject->clearComponents();

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32           ver{ 0 };
        if ( XmlSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, xmlString, kObjectReflectedSchemaVersion,
                                                  nullptr, nullptr, ctx ) == false )
            return false;

        if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
            pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

        pGameObject->setActive( pGameObject->isActive() );
        pGameObject->applyLoadedHierarchy();
        return true;
    }

    bool ObjectStateSerializer::loadFromJsonString( GameObject* pGameObject, string_view jsonString )
    {
        if ( pGameObject == nullptr || jsonString.empty() )
            return false;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        const hashed_string oldName = pGameObject->getName();
        pGameObject->clearComponents();

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32           ver{ 0 };
        if ( JsonSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, jsonString, kObjectReflectedSchemaVersion,
                                                   nullptr, nullptr, ctx ) == false )
            return false;

        if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
            pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

        pGameObject->setActive( pGameObject->isActive() );
        pGameObject->applyLoadedHierarchy();
        return true;
    }

    bool ObjectStateSerializer::rebindSceneHierarchy( GameObject* pGameObject, string_view xmlString )
    {
        (void)xmlString;
        if ( pGameObject == nullptr )
            return false;

        pGameObject->applyLoadedHierarchy();
        return true;
    }

    bool ObjectStateSerializer::rebindSceneHierarchyFromJson( GameObject* pGameObject, string_view jsonString )
    {
        (void)jsonString;
        if ( pGameObject == nullptr )
            return false;

        pGameObject->applyLoadedHierarchy();
        return true;
    }

    bool ObjectStateSerializer::saveToXmlFile( const GameObject* pGameObject, string_view filePath )
    {
        string xmlStr = saveToXmlString( pGameObject );
        if ( xmlStr.empty() )
            return false;

        return FileUtil::writeFile( string{ filePath }, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() );
    }

    bool ObjectStateSerializer::loadFromXmlFile( GameObject* pGameObject, string_view filePath )
    {
        vector<uint8> listData;
        if ( ResourceUtil::readBinaryResource( filePath, listData ) == false && FileUtil::readFile( string{ filePath }, listData ) == false )
            return false;
        if ( listData.empty() )
            return false;

        string_view xmlStr( reinterpret_cast<const utf8*>( listData.data() ), listData.size() );
        return loadFromXmlString( pGameObject, xmlStr );
    }

} // namespace sw
