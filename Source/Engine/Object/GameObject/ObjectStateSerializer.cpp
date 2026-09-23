#include "pch.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Uuid/Uuid.h"

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
            /**
             * @brief 상태를 읽은 오브젝트를 주변에 다시 맞춥니다. 이름이 바뀌었으면 매니저의 이름 표를, 그리고 활성 계층과 읽은 부모 연결을 맞춥니다.
             * @details XML · JSON · 바이너리 세 로더가 이 다섯 줄을 각자 들고 있었습니다. 한 포맷만 빠뜨리면 그 포맷으로 되돌린 오브젝트만
             *          이름으로 찾을 수 없게 됩니다.
             */
            static void finishLoad( GameObject* pGameObject, hashed_string oldName )
            {
                if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
                    pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

                pGameObject->setActive( pGameObject->isActive() );
                pGameObject->applyLoadedHierarchy();
            }

            /** @brief 이름으로 컴포넌트를 만들어 소유자에 붙입니다(역직렬화 팩토리). */
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

            /** @brief 프로세스 토큰을 새로 정합니다(`Uuid` 의 무작위 바이트 8 개). 0 은 "토큰 없음" 과 구분되지 않아 피합니다. */
            static uint64 makeProcessToken()
            {
                const Uuid uuid  = Uuid::generate();
                uint64     token = 0;
                Memory::copy( &token, uuid._arrBytes, sizeof( token ) );
                return ( token != 0 ) ? token : 1;
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
        // 없고(씬이 나중에 rebind 한다), 이름 · 활성 · 태그 · 컴포넌트 · 컴포넌트 간 부착은 모두
        // `_name` / `_bActive` / `_listComponent` 로 실린다. XML · JSON 과 같은 상태다.
        string parentName;
        if ( pGameObject->getParent() != nullptr )
            parentName = pGameObject->getParent()->getName().c_str();
        writer.writeString( parentName );

        // 본문 크기를 앞에 둔다. 세이브 게임은 오브젝트를 이어 붙여 놓고 하나씩 끊어 읽는다.
        const size_t sizeHeaderPos = writer.getOffset();
        writer.write( static_cast<uint32>( 0 ) );

        const size_t     bodyStart = writer.getOffset();
        SerializeContext ctx       = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
        BinarySerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, outBuffer, ctx );
        writer.writeAt( sizeHeaderPos, static_cast<uint32>( writer.getOffset() - bodyStart ) );

        return true;
    }

    size_t ObjectStateSerializer::loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, string& outParentName,
                                                        const ObjectIdentity* pIdentity )
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

        const GameObject::ComponentIdRestoreScope restoreScope( pGameObject, pIdentity );
        SerializeContext                          ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32                                    ver{ 0 };
        if ( BinarySerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, pData + bodyStart, bodySize,
                                                     kObjectReflectedSchemaVersion, nullptr, nullptr, ctx ) == false )
            return 0;

        ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );

        return bodyStart + bodySize;
    }

    bool ObjectStateSerializer::loadFromXmlString( GameObject* pGameObject, string_view xmlString, const ObjectIdentity* pIdentity )
    {
        if ( pGameObject == nullptr || xmlString.empty() )
            return false;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        const hashed_string oldName = pGameObject->getName();
        pGameObject->clearComponents();

        const GameObject::ComponentIdRestoreScope restoreScope( pGameObject, pIdentity );
        SerializeContext                          ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32                                    ver{ 0 };
        if ( XmlSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, xmlString, kObjectReflectedSchemaVersion,
                                                  nullptr, nullptr, ctx ) == false )
            return false;

        ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );
        return true;
    }

    bool ObjectStateSerializer::loadFromJsonString( GameObject* pGameObject, string_view jsonString, const ObjectIdentity* pIdentity )
    {
        if ( pGameObject == nullptr || jsonString.empty() )
            return false;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        const hashed_string oldName = pGameObject->getName();
        pGameObject->clearComponents();

        const GameObject::ComponentIdRestoreScope restoreScope( pGameObject, pIdentity );
        SerializeContext                          ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32                                    ver{ 0 };
        if ( JsonSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, jsonString, kObjectReflectedSchemaVersion,
                                                   nullptr, nullptr, ctx ) == false )
            return false;

        ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );
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

    ObjectIdentity ObjectStateSerializer::captureIdentity( const GameObject* pGameObject )
    {
        ObjectIdentity identity;
        if ( pGameObject == nullptr )
            return identity;

        identity._objectId = pGameObject->getObjectId();
        identity._listComponent.reserve( pGameObject->getComponents().size() );
        for ( const Component* pComp : pGameObject->getComponents() )
        {
            if ( pComp == nullptr || pComp->isPendingKill() )
                continue;
            identity._listComponent.push_back( ObjectIdentity::ComponentEntry{ pComp->getComponentName(), pComp->getComponentId() } );
        }
        return identity;
    }

    void ObjectStateSerializer::writeIdentity( const ObjectIdentity& identity, vector<uint8>& outBuffer )
    {
        BinaryStreamWriter writer( outBuffer );
        writer.write( identity._objectId );
        writer.write( static_cast<uint32>( identity._listComponent.size() ) );
        for ( const ObjectIdentity::ComponentEntry& entry : identity._listComponent )
        {
            writer.writeString( entry._typeName.c_str() );
            writer.write( entry._componentId );
        }
    }

    size_t ObjectStateSerializer::readIdentity( const uint8* pData, size_t size, ObjectIdentity& outIdentity )
    {
        outIdentity = ObjectIdentity{};
        if ( pData == nullptr || size == 0 )
            return 0;

        BinaryStreamReader reader( pData, size );
        uint32             componentCount{ 0 };
        if ( reader.read( outIdentity._objectId ) == false || reader.read( componentCount ) == false )
            return 0;

        // 항목 하나는 적어도 이름 길이(4) + ID(8) 바이트다. 남은 바이트로 담을 수 없는 개수는 망가진 데이터다.
        // 그대로 `reserve` 하면 그 한 줄이 먼저 터진다(`GameInstanceBase::deserializeSceneObjects` 가 같은 이유로 같은 계산을 한다).
        constexpr size_t kMinBytesPerEntry = sizeof( uint32 ) + sizeof( uint64 );
        if ( componentCount > ( size - reader.getOffset() ) / kMinBytesPerEntry )
            return 0;

        outIdentity._listComponent.reserve( componentCount );
        for ( uint32 entryIndex = 0; entryIndex < componentCount; ++entryIndex )
        {
            string                         typeName;
            ObjectIdentity::ComponentEntry entry;
            if ( reader.readString( typeName ) == false || reader.read( entry._componentId ) == false )
                return 0;
            entry._typeName = hashed_string( typeName.data(), static_cast<uint32>( typeName.size() ) );
            outIdentity._listComponent.push_back( entry );
        }
        return reader.getOffset();
    }

    uint64 ObjectStateSerializer::getProcessToken()
    {
        static const uint64 s_processToken = ObjectStateSerializerInternal::makeProcessToken();
        return s_processToken;
    }

} // namespace sw
