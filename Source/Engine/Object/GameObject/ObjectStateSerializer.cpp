#include "pch.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
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
             * @brief 이 오브젝트의 씬 컴포넌트에 붙어 있던 **다른 오브젝트의** 자식 하나입니다 — 제자리에 다시 읽는 동안 떨어졌다가 되붙습니다.
             * @details 부모는 핸들이 아니라 안정 키(`타입#n`)로 적습니다. 식별 목록 없이 다시 읽으면 컴포넌트 id 가 바뀌고, 이름 기반
             *          부착 필드는 이름을 되돌리는 되돌리기에서 틀립니다. 자식은 핸들입니다(그쪽 오브젝트는 그대로 산다).
             */
            struct ChildLink
            {
                ComponentHandle _child;
                string          _parentKey;
            };

            /**
             * @brief 다시 읽기 전에 다른 오브젝트의 자식 연결을 적습니다.
             * @details 제자리에서 다시 읽으면(되돌리기 · 다시 하기 · 프리팹으로 되돌리기 · 플레이 종료 복원) 컴포넌트를 모두 지우고 새로
             *          만드는데, 씬 컴포넌트의 소멸자가 자식을 떼어 **다른 오브젝트의 자식들이 루트가 됐습니다.** 로드가 되붙이는 것은
             *          이 오브젝트 **안의** 부착(`applyLoadedHierarchy`)뿐이었습니다. 부모 속성 하나를 고치고 되돌리면 자식이 떨어졌습니다.
             */
            static void captureChildLinks( const GameObject* pGameObject, vector<ChildLink>& outListLink )
            {
                outListLink.clear();
                for ( Component* pComp : pGameObject->getComponents() )
                {
                    SceneComponent* pScene = ( pComp != nullptr && pComp->isPendingDestroy() == false ) ? castTo<SceneComponent>( pComp ) : nullptr;
                    if ( pScene == nullptr )
                        continue;
                    for ( SceneComponent* pChild : pScene->getChildren() )
                    {
                        if ( pChild == nullptr || pChild->getOwner() == nullptr || pChild->getOwner() == pGameObject )
                            continue;
                        outListLink.push_back( ChildLink{ pChild->getHandle(), ComponentStableKey::makeKey( pScene ) } );
                    }
                }
            }

            /**
             * @brief 적어 둔 자식을 새로 만든 같은 자리의 부모(안정 키)에 되붙입니다. 그 자리가 없으면 primary 에, 그것도 없으면 루트로 둡니다.
             * @details 자식의 로컬 트랜스폼은 그대로라 부모가 같은 값으로 돌아오면 월드도 그대로입니다. 붙이는 자리가 계층 활성도 맞춥니다.
             */
            static void restoreChildLinks( GameObject* pGameObject, const vector<ChildLink>& listLink )
            {
                GameObjectManager* pManager = pGameObject->getManager();
                if ( pManager == nullptr )
                    return;
                for ( const ChildLink& link : listLink )
                {
                    SceneComponent* pChild = castTo<SceneComponent>( pManager->resolveComponent( link._child ) );
                    if ( pChild == nullptr )
                        continue;
                    SceneComponent* pParent = castTo<SceneComponent>( ComponentStableKey::findComponent( pGameObject, link._parentKey ) );
                    if ( pParent == nullptr )
                        pParent = pGameObject->getPrimarySceneComponent();
                    if ( pParent == nullptr )
                    {
                        SW_LOG_WARNING( "In-place load of '%#' left child '%#' as a root (no scene component to attach to)",
                                        pGameObject->getName().c_str(), pChild->getOwner() != nullptr ? pChild->getOwner()->getName().c_str() : "?" );
                        continue;
                    }
                    if ( pChild->getParent() != pParent )
                        pChild->attachToComponent( pParent );
                }
            }

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
                if ( pComp == nullptr || pComp->isPendingDestroy() )
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
    template <typename TSerializer>
    string ObjectStateSerializer::saveToText( const GameObject* pGameObject )
    {
        if ( pGameObject == nullptr )
            return {};

        pGameObject->prepareSerialize();

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return {};

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
        return TSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
    }

    template <typename DeserializeStateFunc>
    bool ObjectStateSerializer::loadStateInPlace( GameObject* pGameObject, const ObjectIdentity* pIdentity, DeserializeStateFunc&& deserializeState )
    {
        const hashed_string                              oldName = pGameObject->getName();
        vector<ObjectStateSerializerInternal::ChildLink> listChildLink;
        ObjectStateSerializerInternal::captureChildLinks( pGameObject, listChildLink );
        pGameObject->clearComponents();

        const GameObject::ComponentIdRestoreScope restoreScope( pGameObject, pIdentity );
        const SerializeContext                    ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32                                    version{ 0 };
        const bool                                bLoaded = deserializeState( version, ctx );
        if ( bLoaded )
            ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );
        ObjectStateSerializerInternal::restoreChildLinks( pGameObject, listChildLink );
        return bLoaded;
    }

    template <typename TSerializer>
    bool ObjectStateSerializer::loadFromText( GameObject* pGameObject, string_view text, const ObjectIdentity* pIdentity )
    {
        if ( pGameObject == nullptr || text.empty() )
            return false;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        return loadStateInPlace( pGameObject, pIdentity, [&]( uint32& outVersion, const SerializeContext& ctx )
        {
            return TSerializer::deserializeVersioned( outVersion, pGameObject, *pTypeInfo, text, kObjectReflectedSchemaVersion, nullptr, nullptr, ctx );
        } );
    }

    string ObjectStateSerializer::saveToXmlString( const GameObject* pGameObject )
    {
        return saveToText<XmlSerializer>( pGameObject );
    }

    string ObjectStateSerializer::saveToJsonString( const GameObject* pGameObject )
    {
        return saveToText<JsonSerializer>( pGameObject );
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

        const bool bLoaded = loadStateInPlace( pGameObject, pIdentity, [&]( uint32& outVersion, const SerializeContext& ctx )
        {
            return BinarySerializer::deserializeVersioned( outVersion, pGameObject, *pTypeInfo, pData + bodyStart, bodySize,
                                                           kObjectReflectedSchemaVersion, nullptr, nullptr, ctx );
        } );
        return bLoaded ? bodyStart + bodySize : 0;
    }

    bool ObjectStateSerializer::loadFromXmlString( GameObject* pGameObject, string_view xmlString, const ObjectIdentity* pIdentity )
    {
        return loadFromText<XmlSerializer>( pGameObject, xmlString, pIdentity );
    }

    bool ObjectStateSerializer::loadFromJsonString( GameObject* pGameObject, string_view jsonString, const ObjectIdentity* pIdentity )
    {
        return loadFromText<JsonSerializer>( pGameObject, jsonString, pIdentity );
    }

    bool ObjectStateSerializer::rebindSceneHierarchy( GameObject* pGameObject )
    {
        if ( pGameObject == nullptr )
            return false;

        pGameObject->applyLoadedHierarchy();
        return true;
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
            if ( pComp == nullptr || pComp->isPendingDestroy() )
                continue;
            // 타입 이름으로 적는다 — 되살릴 때(`takeRestoredComponentId`) 새 컴포넌트의 타입 이름과 견준다. 이름표를 적으면 이름표를 단
            // 컴포넌트는 id 를 되찾지 못해 되돌리기 뒤 핸들이 끊겼다.
            identity._listComponent.push_back( ObjectIdentity::ComponentEntry{ pComp->getTypeName(), pComp->getComponentId() } );
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
