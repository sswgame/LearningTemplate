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
#include "Engine/Serialization/Core/SchemaMigrate.h"
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
                    if ( pChild->getParent() != pParent && pChild->attachToComponent( pParent ) == false )
                        SW_LOG_WARNING( "In-place load of '%#' could not re-attach child '%#'", pGameObject->getName().c_str(),
                                        pChild->getOwner() != nullptr ? pChild->getOwner()->getName().c_str() : "?" );
                }
            }

            /**
             * @brief 상태를 읽은 오브젝트를 주변에 다시 맞춥니다. 이름이 바뀌었으면 매니저의 이름 표를, 그리고 활성 계층을 맞추고 컴포넌트에 알립니다.
             * @details XML · JSON · 바이너리 세 로더가 이 다섯 줄을 각자 들고 있었습니다. 한 포맷만 빠뜨리면 그 포맷으로 되돌린 오브젝트만
             *          이름으로 찾을 수 없게 됩니다. 부모 연결은 여기서 잇지 않습니다 — 묶음(`ObjectStateBatch::finish`)이 모두 읽은 뒤 잇습니다.
             */
            static void finishLoad( GameObject* pGameObject, hashed_string oldName )
            {
                if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
                    pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

                pGameObject->setActive( pGameObject->isActive() );
                // 값을 다 읽었다 — 컴포넌트가 값을 자원으로 바꾼다(`Component::onPostLoad`). 편집 중에도 불린다.
                for ( Component* pComp : pGameObject->getComponents() )
                {
                    if ( pComp != nullptr && pComp->isPendingDestroy() == false )
                        pComp->onPostLoad();
                }
            }

            /** @brief 상태가 이 오브젝트를 부를 때의 id 입니다. 문맥이 따로 주지 않았으면 되살리는 원래 id 입니다. */
            static uint64 resolveSavedId( const ObjectLoadContext& context )
            {
                if ( context._savedId != 0 )
                    return context._savedId;
                return ( context._pIdentity != nullptr ) ? context._pIdentity->_objectId : 0;
            }

            /**
             * @brief 이름으로 컴포넌트를 만들어 소유자에 붙입니다(역직렬화 팩토리).
             * @details 모르는 타입(게임 모듈이 안 올라왔다 · 이름을 바꿨는데 별칭이 없다)은 만들지 못하고 그 데이터는 버려진다. 예전에는 **경고 없이**
             *          버려, 그 상태로 저장하면 컴포넌트가 파일에서 영영 사라졌다. 이제 어느 오브젝트의 어느 타입인지 경고한다.
             */
            static void* createOwnedComponent( void* pOuter, hashed_string typeName )
            {
                GameObject* pGameObject = static_cast<GameObject*>( pOuter );
                if ( pGameObject == nullptr || pGameObject->getManager() == nullptr )
                    return nullptr;
                Component* pCreated = pGameObject->getManager()->addComponentByName( pGameObject, typeName, false );
                if ( pCreated == nullptr )
                    SW_LOG_WARNING( "'%#' has a component of unknown type '%#' - it is not created and saving now would drop it",
                                    pGameObject->getName().c_str(), typeName.c_str() );
                return pCreated;
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
                // 지금 타입에 없는 칸은 건너뛰고 읽는다 — 세 형식이 같은 규칙이다. 예전에는 XML · JSON 만 건너뛰고 바이너리는 **오브젝트 통째로**
                // 실패해, 컴포넌트 PROPERTY 하나를 지우면 그 컴포넌트를 가진 모든 오브젝트의 세이브 · 플레이 스냅샷(핫 리로드 뒤 Stop)을 읽지 못했다.
                ctx.setAllowUnknownProperties( true );
                return ctx;
            }

            /**
             * @brief 오브젝트 자기 칸 가운데 지금 타입에 없는 것(옛 상태에만 있는 필드)을 알리고 넘깁니다. 스키마 버전이 같을 때만 받습니다.
             * @details 바이너리의 판 붙은 읽기는 남는 칸이 있으면 이관 함수를 부르고, 없으면 실패합니다. 오브젝트 상태에는 이관할 것이 없다 —
             *          지운 칸은 버리고 나머지를 읽는 것이 XML · JSON 과 같은 규칙입니다. 버전이 다르면 받지 않습니다(진짜 이관이 필요하다).
             */
            static bool skipFieldsTheTypeNoLongerHas( const SchemaMigrateContext& migrateContext )
            {
                if ( migrateContext._fromVersion != migrateContext._toVersion )
                    return false;
                const size_t orphanCount = migrateContext._pOrphans != nullptr ? migrateContext._pOrphans->size() : 0;
                if ( orphanCount > 0 && migrateContext._pTypeInfo != nullptr )
                    SW_LOG_WARNING( "%#: %# saved value(s) are not in the type any more - skipped", migrateContext._pTypeInfo->_name.c_str(),
                                    static_cast<uint64>( orphanCount ) );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    template <typename TSerializer>
    string ObjectStateSerializer::saveToText( const GameObject* pGameObject, const ObjectSaveOptions& options )
    {
        if ( pGameObject == nullptr )
            return {};

        pGameObject->prepareSerialize( options );

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return {};

        SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
        return TSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
    }

    template <typename DeserializeStateFunc>
    bool ObjectStateSerializer::loadStateInPlace( GameObject* pGameObject, const ObjectLoadContext& context, DeserializeStateFunc&& deserializeState )
    {
        // 제자리 읽기는 컴포넌트를 모두 비우고 다시 만든다 — 컴포넌트 틱 중에는 할 수 없다(다른 워커가 그 컴포넌트를 틱한다). 결과를 바로 돌려줘야
        // 하므로 미루지 않고 거절한다. 틱 안에서는 `GameObjectManager::executeOrDeferPostTick` 으로 감싸 부를 것.
        const GameObjectManager* pManager = pGameObject->getManager();
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            SW_LOG_ERROR( "State of '%#' cannot be loaded during the component tick - defer it (executeOrDeferPostTick)", pGameObject->getName().c_str() );
            return false;
        }

        const hashed_string                              oldName = pGameObject->getName();
        vector<ObjectStateSerializerInternal::ChildLink> listChildLink;
        ObjectStateSerializerInternal::captureChildLinks( pGameObject, listChildLink );

        // 읽기 전 상태를 찍어 둔다 — 실패하면 그것으로 되돌린다. 컴포넌트가 없는 오브젝트(새로 만든 것 — 씬 로드 · 복제)는 찍을 것이 없다.
        vector<uint8>  previousBytes;
        ObjectIdentity previousIdentity;
        if ( context._bRestorePreviousOnFailure && pGameObject->getComponents().empty() == false )
        {
            previousIdentity = captureIdentity( pGameObject );
            if ( saveToBinaryBuffer( pGameObject, previousBytes ) == false )
                previousBytes.clear();
        }
        pGameObject->clearComponents();

        const GameObject::ComponentIdRestoreScope restoreScope( pGameObject, context._pIdentity );
        const SerializeContext                    ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
        uint32                                    version{ 0 };
        const bool                                bLoaded = deserializeState( version, ctx );
        if ( bLoaded )
        {
            // 상태에 적힌 이름 — 매니저가 유일하게 바꾸기(`finishLoad`) 전에 잡는다. 옛 데이터는 자기 안의 부착에 이 이름을 적었다.
            const hashed_string savedName = pGameObject->getName();
            ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );
            const uint64 savedId = ObjectStateSerializerInternal::resolveSavedId( context );
            if ( context._pBatch != nullptr )
            {
                context._pBatch->add( pGameObject, savedId, savedName, context._bExternalParentAllowed );
            }
            else
            {
                // 혼자 읽는 상태도 같은 규칙으로 잇는다 — 같은 실행의 상태라 다른 오브젝트는 매니저의 런타임 id 로 찾는다.
                ObjectStateBatch single( ObjectIdSpace::Live );
                single.add( pGameObject, savedId, savedName, context._bExternalParentAllowed );
                single.finish();
            }
        }
        else if ( previousBytes.empty() == false )
        {
            SW_LOG_WARNING( "State of '%#' could not be read - it is restored to what it was before the load", oldName.c_str() );
            ObjectLoadContext restoreContext{};
            restoreContext._pIdentity                 = &previousIdentity;
            restoreContext._bRestorePreviousOnFailure = false;
            if ( loadFromBinaryBuffer( pGameObject, previousBytes.data(), previousBytes.size(), restoreContext ) == 0 )
                SW_LOG_ERROR( "State of '%#' could not be restored either - the object is left empty", oldName.c_str() );
        }
        ObjectStateSerializerInternal::restoreChildLinks( pGameObject, listChildLink );
        return bLoaded;
    }

    template <typename TSerializer>
    bool ObjectStateSerializer::loadFromText( GameObject* pGameObject, string_view text, const ObjectLoadContext& context )
    {
        if ( pGameObject == nullptr || text.empty() )
            return false;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        return loadStateInPlace( pGameObject, context, [&]( uint32& outVersion, const SerializeContext& ctx )
        {
            return TSerializer::deserializeVersioned( outVersion, pGameObject, *pTypeInfo, text, kObjectReflectedSchemaVersion, nullptr, nullptr, ctx );
        } );
    }

    string ObjectStateSerializer::saveToXmlString( const GameObject* pGameObject, const ObjectSaveOptions& options )
    {
        return saveToText<XmlSerializer>( pGameObject, options );
    }

    string ObjectStateSerializer::saveToJsonString( const GameObject* pGameObject, const ObjectSaveOptions& options )
    {
        return saveToText<JsonSerializer>( pGameObject, options );
    }

    bool ObjectStateSerializer::saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer, const ObjectSaveOptions& options )
    {
        if ( pGameObject == nullptr )
            return false;

        pGameObject->prepareSerialize( options );

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return false;

        BinaryStreamWriter writer( outBuffer );

        // **바깥의 부모 이름은 옛 세이브 형식의 칸일 뿐이다.** 부모는 씬 컴포넌트의 부착 필드(`_attachOwnerId` · `_attachComponent`)로
        // 상태 안에 든다 — 읽는 쪽은 이 칸을 읽고 버린다. 세이브 · 핫 리로드 스냅샷의 형식을 바꾸지 않으려고 그대로 쓴다.
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

    size_t ObjectStateSerializer::loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, const ObjectLoadContext& context )
    {
        if ( pGameObject == nullptr || pData == nullptr || size == 0 )
            return 0;

        const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
        if ( pTypeInfo == nullptr )
            return 0;

        BinaryStreamReader reader( pData, size );
        string             legacyParentName; // 옛 칸 — 읽고 버린다(`saveToBinaryBuffer` 설명)
        if ( reader.readString( legacyParentName ) == false )
            return 0;

        uint32 bodySize{ 0 };
        if ( reader.read( bodySize ) == false )
            return 0;

        const size_t bodyStart = reader.getOffset();
        if ( bodyStart + bodySize > size )
            return 0;

        const bool bLoaded = loadStateInPlace( pGameObject, context, [&]( uint32& outVersion, const SerializeContext& ctx )
        {
            return BinarySerializer::deserializeVersioned( outVersion, pGameObject, *pTypeInfo, pData + bodyStart, bodySize,
                                                           kObjectReflectedSchemaVersion, &ObjectStateSerializerInternal::skipFieldsTheTypeNoLongerHas, nullptr, ctx );
        } );
        return bLoaded ? bodyStart + bodySize : 0;
    }

    bool ObjectStateSerializer::loadFromXmlString( GameObject* pGameObject, string_view xmlString, const ObjectLoadContext& context )
    {
        return loadFromText<XmlSerializer>( pGameObject, xmlString, context );
    }

    bool ObjectStateSerializer::loadFromJsonString( GameObject* pGameObject, string_view jsonString, const ObjectLoadContext& context )
    {
        return loadFromText<JsonSerializer>( pGameObject, jsonString, context );
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

    // ======================================================================
    // ObjectStateBatch: 모두 읽은 뒤 부착을 한 번에 잇는다
    // ======================================================================

    ObjectStateBatch::ObjectStateBatch( ObjectIdSpace idSpace )
        : _listEntry{}
        , _mapSavedIdToObject{}
        , _mapSavedNameToEntry{}
        , _idSpace{ idSpace }
        , _bFinished{ false }
    {
    }

    ObjectStateBatch::~ObjectStateBatch()
    {
        // 적어 놓고 잇지 않으면 그 오브젝트들은 루트로 남는다 — 부른 쪽이 `finish` 를 빠뜨렸다.
        SW_ASSERT( _listEntry.empty() || _bFinished );
    }

    void ObjectStateBatch::add( GameObject* pObject, uint64 savedId, hashed_string savedName, bool bExternalParentAllowed )
    {
        if ( pObject == nullptr )
            return;
        SW_ASSERT( _bFinished == false );

        const uint32 entryIndex = static_cast<uint32>( _listEntry.size() );
        _listEntry.push_back( Entry{ pObject, savedId, savedName, bExternalParentAllowed } );
        // 같은 id · 이름이 둘이면 먼저 적힌 것이다(옛 문서에는 이름이 겹친 엔티티가 있을 수 있다).
        if ( savedId != 0 )
            _mapSavedIdToObject.emplace( savedId, pObject );
        if ( savedName.empty() == false )
            _mapSavedNameToEntry.emplace( savedName, entryIndex );
    }

    void ObjectStateBatch::finish()
    {
        if ( _bFinished )
            return;
        _bFinished = true;

        // 1) 이름. 읽는 동안 다른 오브젝트가 잠시 쥐고 있던 저장된 이름이 이제 비었으면 되찾는다 — 플레이 중 이름을 서로 바꾼 오브젝트를
        //    되돌리면 앞의 것이 뒤의 것 이름을 잠시 쥐고 있어, 앞의 것이 영영 `Left_2` 로 남았다. 아직 쥔 오브젝트가 있으면 그대로다.
        for ( const Entry& entry : _listEntry )
        {
            GameObject* pObject = entry._pObject;
            if ( pObject->getManager() == nullptr || pObject->isPendingDestroy() || entry._savedName.empty() || pObject->getName() == entry._savedName )
                continue;
            pObject->setName( entry._savedName ); // 아직 다른 오브젝트가 쥐고 있으면 매니저가 지금 이름(번호)을 그대로 둔다
        }

        // 2) 부착. 모든 오브젝트가 생긴 뒤라 자식이 부모보다 먼저 읽혔어도 부모를 찾는다.
        for ( const Entry& entry : _listEntry )
            resolveEntry( entry );
    }

    GameObject* ObjectStateBatch::findBySavedId( uint64 savedId ) const
    {
        const auto mapIt = _mapSavedIdToObject.find( savedId );
        return ( mapIt != _mapSavedIdToObject.end() ) ? mapIt->second : nullptr;
    }

    GameObject* ObjectStateBatch::findBySavedName( hashed_string savedName ) const
    {
        const auto mapIt = _mapSavedNameToEntry.find( savedName );
        return ( mapIt != _mapSavedNameToEntry.end() ) ? _listEntry[mapIt->second]._pObject : nullptr;
    }

    GameObject* ObjectStateBatch::findAttachOwner( const Entry& entry, hashed_string ownerName, uint64 ownerId ) const
    {
        if ( ownerId == 0 )
        {
            // 소유자 칸이 비었으면 자기다. 옛 데이터는 자기 안의 부착에도 자기 이름을 적었다 — 읽기 전 이름(저장된 이름)과 견준다.
            if ( ownerName.empty() || ownerName == entry._savedName )
                return entry._pObject;
            // id 가 없는 옛 데이터. **이 묶음의 저장된 이름**에서만 찾는다 — 매니저에서 찾으면 유일하게 바뀐 이름 때문에 다른 오브젝트에 붙는다.
            return findBySavedName( ownerName );
        }
        if ( ownerId == entry._savedId )
            return entry._pObject;

        GameObject* pOwner = findBySavedId( ownerId );
        if ( pOwner != nullptr )
            return pOwner;
        // 같은 실행의 상태면 묶음 밖(이미 살아 있는 부모 — 되돌리기 · 복제)도 런타임 id 로 찾는다. 파일 id 는 이 실행의 id 와 우연히 같을 수 있어 찾지 않는다.
        if ( _idSpace == ObjectIdSpace::Live && entry._pObject->getManager() != nullptr )
            return entry._pObject->getManager()->findGameObjectById( ownerId );
        return nullptr;
    }

    void ObjectStateBatch::resolveEntry( const Entry& entry ) const
    {
        GameObject* pObject = entry._pObject;
        if ( pObject->isPendingDestroy() )
            return;

        for ( Component* pComp : pObject->getComponents() )
        {
            if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isSceneComponent() == false )
                continue;
            SceneComponent*      pScene    = static_cast<SceneComponent*>( pComp );
            SceneAttachReference reference = pScene->getLoadedAttachReference();
            if ( reference._componentKey.empty() )
                continue; // 루트
            reference._idSpace = _idSpace;

            GameObject* pOwner    = findAttachOwner( entry, reference._ownerName, reference._ownerId );
            const bool  bExternal = pOwner != pObject;
            if ( bExternal && entry._bExternalParentAllowed == false )
                continue; // 프리팹 — 루트에는 부모가 없다

            Component*      pParentComp = ( pOwner != nullptr ) ? ComponentStableKey::findComponent( pOwner, reference._componentKey.c_str() ) : nullptr;
            SceneComponent* pParent     = ( pParentComp != nullptr && pParentComp->isSceneComponent() ) ? static_cast<SceneComponent*>( pParentComp ) : nullptr;
            if ( pParent != nullptr && pParent != pScene && pScene->attachToComponent( pParent ) )
                continue;

            // 찾지 못했다 — 지우지 않고 남긴다. 다음 저장이 그대로 다시 쓰고, 부모가 돌아오면(프리팹을 되찾았다) 다음 로드가 붙인다.
            SW_LOG_WARNING( "'%#' keeps its parent reference to '%#' (id %#, %#) - the parent is not loaded", pObject->getName().c_str(),
                            reference._ownerName.empty() ? "self" : reference._ownerName.c_str(), reference._ownerId, reference._componentKey.c_str() );
            pScene->keepUnresolvedAttach( reference );
        }
    }

} // namespace sw
