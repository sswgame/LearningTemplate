#include "pch.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectValidation.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Base/BinaryStream.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/Serializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

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
             *          만드는데, 씬 컴포넌트의 소멸자가 자식을 떼어 **다른 오브젝트의 자식들이 루트가 됩니다.** 로드는 이 오브젝트 **안의**
             *          부착만 되붙이므로, 이것이 없으면 부모 속성 하나를 고치고 되돌릴 때 자식이 떨어집니다.
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
             * @brief 상태를 읽은 오브젝트를 주변에 다시 맞춥니다. 이름이 바뀌었으면 매니저의 이름 표를, 그리고 활성 계층을 맞춥니다.
             * @details XML · JSON · 바이너리 세 로더가 함께 씁니다. 한 포맷만 빠뜨리면 그 포맷으로 되돌린 오브젝트만
             *          이름으로 찾을 수 없게 됩니다. 부모 연결 · 핸들 · 컴포넌트 알림(`onPostLoad`)은 여기서 하지 않습니다 — 묶음
             *          (`ObjectStateBatch::finish`)이 모두 읽은 뒤 합니다.
             */
            static void finishLoad( GameObject* pGameObject, hashed_string oldName )
            {
                if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
                    pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

                pGameObject->setActive( pGameObject->isActive() );
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
             * @details 모르는 타입(게임 모듈이 안 올라왔다 · 이름을 바꿨는데 별칭이 없다)은 만들지 못한다 — 어느 오브젝트의 어느 타입인지 경고하고,
             *          원문은 `keepMissingComponent` 가 맡는다.
             */
            static void* createOwnedComponent( void* pOuter, hashed_string typeName )
            {
                GameObject* pGameObject = static_cast<GameObject*>( pOuter );
                if ( pGameObject == nullptr || pGameObject->getManager() == nullptr )
                    return nullptr;
                // 만들지 못하면 직렬화기가 원문을 `keepMissingComponent` 에 맡긴다(경고도 그쪽이 한다).
                return pGameObject->getManager()->addComponentByName( pGameObject, typeName, false );
            }

            /**
             * @brief 모르는 타입의 컴포넌트 원문을 `MissingComponent` 로 맡습니다. 건너뛰면 그대로 저장할 때 그 값이 영영 사라진다.
             */
            static bool keepMissingComponent( void* pOuter, const SerializeContext::OpaqueElementView& element )
            {
                GameObject* pGameObject = static_cast<GameObject*>( pOuter );
                if ( pGameObject == nullptr )
                    return false;
                MissingComponent* pMissing = pGameObject->addComponent<MissingComponent>();
                if ( pMissing == nullptr )
                    return false;
                pMissing->setOriginalElement( element );
                SW_LOG_WARNING( "'%#' has a component of unknown type '%#' - kept as MissingComponent, saving writes its data back unchanged",
                                pGameObject->getName().c_str(), element._typeName );
                return true;
            }

            /** @brief 원소가 `MissingComponent` 면 맡은 원문을 돌려줍니다 — 직렬화기가 같은 형식이면 그대로 다시 씁니다. */
            static bool queryMissingComponent( const void* pElement, SerializeContext::OpaqueElementView& outElement )
            {
                const Component* pComp = static_cast<const Component*>( pElement );
                if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->getTypeInfo() != MissingComponent::StaticType() )
                    return false;
                return static_cast<const MissingComponent*>( pComp )->tryGetOriginalElement( outElement );
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

            static SerializeContext makeGameObjectXMLContext( GameObject* pGameObject )
            {
                SerializeContext ctx = SerializeContext::deriveFromDefault();
                ctx.setOuterInstance( pGameObject );
                ctx.setOwnedPointerFactory( &createOwnedComponent );
                ctx.setRuntimeTypeInfoFn( &getComponentRuntimeTypeInfo );
                ctx.setOpaqueElementHandlers( &keepMissingComponent, &queryMissingComponent );
                // 지금 타입에 없는 칸은 건너뛰고 읽는다 — 세 형식이 같은 규칙이다. 바이너리가 실패하면 컴포넌트 PROPERTY 하나를 지웠을 때 그
                // 컴포넌트를 가진 모든 오브젝트의 세이브 · 플레이 스냅샷(핫 리로드 뒤 Stop)을 읽지 못한다.
                ctx.setAllowUnknownProperties( true );
                return ctx;
            }

            /**
             * @brief 오브젝트 자기 칸 가운데 지금 타입에 없는 것(지운 필드)을 넘깁니다. 스키마 버전이 같을 때만 받습니다.
             * @details 바이너리의 판 붙은 읽기는 남는 칸이 있으면 이관 함수를 부르고, 없으면 실패합니다. 오브젝트 상태에는 이관할 것이 없다 —
             *          지운 칸은 버리고 나머지를 읽는 것이 XML · JSON 과 같은 규칙입니다. 버전이 다르면 받지 않습니다(진짜 이관이 필요하다).
             *          버린 칸은 여기서 알리지 않습니다 — 세 형식이 같이 지나는 `runSchemaMigrateStep` 이 이관이 찾아 보지 않은 칸을
             *          이름과 함께 로드마다 한 번 알립니다(여기서도 알리면 같은 일이 두 줄이 됩니다).
             */
            static bool skipFieldsTheTypeNoLongerHas( const SchemaMigrateContext& migrateContext )
            {
                return migrateContext._fromVersion == migrateContext._toVersion;
            }

            /** @brief `GameObjectHandle` 의 타입 이름입니다 — 리플렉션 PROPERTY 의 타입 · 글 처리기의 이름이 같습니다. */
            static const hashed_string& getObjectHandleTypeName()
            {
                static const hashed_string s_typeName{ "GameObjectHandle" };
                return s_typeName;
            }

            /** @brief `ComponentHandle` 의 타입 이름입니다. */
            static const hashed_string& getComponentHandleTypeName()
            {
                static const hashed_string s_typeName{ "ComponentHandle" };
                return s_typeName;
            }

            /**
             * @brief 묶음이 저장된 id 를 옮길 표가 없는 `ComponentHandle` 을 담는 프로퍼티인지 봅니다(단일 값 · 컨테이너의 어느 단계든).
             * @details `GameObjectHandle` 은 어디에 들었든 옮긴다(`ObjectStateBatch::remapContainerHandles`). `ComponentHandle` 은 저장된
             *          컴포넌트 id 를 이 실행의 컴포넌트로 옮길 표가 없다.
             */
            static bool holdsComponentHandle( const PropertyInfo& prop )
            {
                if ( prop._bIsContainer == SW_FALSE )
                    return prop._typeName == getComponentHandleTypeName();
                const NestedContainerInfo shape = prop.getContainerShape();
                for ( const NestedContainerInfo* pLevel = &shape; pLevel != nullptr; pLevel = pLevel->_elementNested.get() )
                {
                    if ( pLevel->_elementTypeName == getComponentHandleTypeName() || pLevel->_keyTypeName == getComponentHandleTypeName() )
                        return true;
                }
                return false;
            }

            /** @brief 컨테이너의 어느 단계(키 · 원소)에 `GameObjectHandle` 이 드는지 봅니다. */
            static bool holdsObjectHandle( const NestedContainerInfo& shape )
            {
                for ( const NestedContainerInfo* pLevel = &shape; pLevel != nullptr; pLevel = pLevel->_elementNested.get() )
                {
                    if ( pLevel->_elementTypeName == getObjectHandleTypeName() || pLevel->_keyTypeName == getObjectHandleTypeName() )
                        return true;
                }
                return false;
            }

            /**
             * @brief 저장할 때 `GameObjectHandle` 값을 저장할 id 로 옮겨 적는 글 · 바이너리 처리기입니다 — 부착과 같은 규칙(`ObjectSaveOptions::getSavedObjectId`).
             * @details 세 형식이 모두 이 처리기를 지납니다. 런타임 id 를 그대로 적으면 씬 파일을 다시 열 때 같은 값을 받은 다른 오브젝트를 가리킬 수 있다.
             *          주의: 핸들은 내장 타입이라 기본 문맥에 8 바이트 바이너리 처리기가 있다 — 글 처리기만 바꾸면 바이너리(쿠킹한 씬 · 세이브)는 런타임
             *          id 를 그대로 싣고, 읽는 묶음이 그 id 를 파일 id 로 찾지 못해 핸들이 비게 된다. 바이너리도 같은 8 바이트 모양으로 바꿔 적는다.
             */
            struct ReferenceWriter
            {
                const ObjectSaveOptions* _pOptions{ nullptr };

                uint64 computeSavedId( const void* pValue ) const
                {
                    const GameObjectHandle& handle = *static_cast<const GameObjectHandle*>( pValue );
                    const bool              bOmit  = handle.isValid() == false || _pOptions->_bOmitExternalParent;
                    return bOmit ? 0 : _pOptions->getSavedObjectId( handle.objectId() );
                }

                string write( const void* pValue ) const { return sw::to_string( computeSavedId( pValue ) ); }

                /** @brief 기본 바이너리 처리기와 같은 모양(핸들 하나의 바이트)으로 저장할 id 를 적습니다 — 읽기는 기본 처리기 그대로다. */
                void writeBinary( const void* pValue, vector<uint8>& outListBuffer ) const
                {
                    const GameObjectHandle savedHandle = GameObjectHandle::make( computeSavedId( pValue ) );
                    const uint8*           pByte       = reinterpret_cast<const uint8*>( &savedHandle );
                    outListBuffer.insert( outListBuffer.end(), pByte, pByte + sizeof( GameObjectHandle ) );
                }
            };

            /** @brief 오브젝트 상태를 쓰는 문맥에 핸들 글 · 바이너리 처리기를 겁니다. 읽기는 기본 그대로다 — 옮기는 일은 묶음이 모두 읽은 뒤 한다(`ObjectStateBatch::finish`). */
            static void registerReferenceWriter( SerializeContext& ctx, const ReferenceWriter& writer )
            {
                const SerializeContext::TextReadFn* pReader = SerializeContext::getDefault().findTextReader( getObjectHandleTypeName() );
                if ( pReader == nullptr )
                    return;
                ctx.registerTextHandler( getObjectHandleTypeName(), SW_DELEGATE_METHOD( SerializeContext::TextWriteFn, &ReferenceWriter::write, &writer ),
                                         *pReader );
                const SerializeContext::BinaryReadFn* pBinaryReader = SerializeContext::getDefault().findBinaryReader( getObjectHandleTypeName() );
                if ( pBinaryReader != nullptr )
                    ctx.registerBinaryHandler( getObjectHandleTypeName(),
                                               SW_DELEGATE_METHOD( SerializeContext::BinaryWriteFn, &ReferenceWriter::writeBinary, &writer ), *pBinaryReader );
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

        // 글로 저장하는 길(씬 · 프리팹 저작)은 쓰기 전에 검증한다 — 결과만 남기고 저장은 그대로 한다. 바이너리(플레이 · 되돌리기 스냅숏)는 보지 않는다.
        (void)ObjectValidation::reportGameObject( *pGameObject, true );

        SerializeContext                                     ctx = ObjectStateSerializerInternal::makeGameObjectXMLContext( const_cast<GameObject*>( pGameObject ) );
        const ObjectStateSerializerInternal::ReferenceWriter referenceWriter{ &options };
        ObjectStateSerializerInternal::registerReferenceWriter( ctx, referenceWriter );
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
        const SerializeContext                    ctx = ObjectStateSerializerInternal::makeGameObjectXMLContext( pGameObject );
        uint32                                    version{ 0 };
        const bool                                bLoaded = deserializeState( version, ctx );
        if ( bLoaded )
        {
            // 상태에 적힌 이름 — 매니저가 유일하게 바꾸기(`finishLoad`) 전에 잡는다. 같은 묶음의 이름만 남은 참조가 이 이름으로 찾는다.
            const hashed_string savedName = pGameObject->getName();
            ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );
            const uint64 savedId = ObjectStateSerializerInternal::resolveSavedId( context );
            if ( context._pBatch != nullptr )
            {
                context._pBatch->addLoadedState( pGameObject, savedId, savedName, context._bExternalParentAllowed );
            }
            else
            {
                // 혼자 읽는 상태도 같은 규칙으로 잇는다 — 같은 실행의 상태라 다른 오브젝트는 매니저의 런타임 id 로 찾는다.
                ObjectStateBatch single( ObjectIdSpace::Live );
                single.addLoadedState( pGameObject, savedId, savedName, context._bExternalParentAllowed );
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

    string ObjectStateSerializer::saveToXMLString( const GameObject* pGameObject, const ObjectSaveOptions& options )
    {
        return saveToText<XMLSerializer>( pGameObject, options );
    }

    string ObjectStateSerializer::saveToJSONString( const GameObject* pGameObject, const ObjectSaveOptions& options )
    {
        return saveToText<JSONSerializer>( pGameObject, options );
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

        // 부모는 따로 적지 않는다 — 씬 컴포넌트의 부착 필드(`_attachOwnerId` · `_attachComponent`)로 상태 안에 든다.
        // 본문 크기를 앞에 둔다. 세이브 게임은 오브젝트를 이어 붙여 놓고 하나씩 끊어 읽는다.
        const size_t sizeHeaderPos = writer.getOffset();
        writer.write( static_cast<uint32>( 0 ) );

        const size_t                                         bodyStart = writer.getOffset();
        SerializeContext                                     ctx       = ObjectStateSerializerInternal::makeGameObjectXMLContext( const_cast<GameObject*>( pGameObject ) );
        const ObjectStateSerializerInternal::ReferenceWriter referenceWriter{ &options };
        ObjectStateSerializerInternal::registerReferenceWriter( ctx, referenceWriter );
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
        uint32             bodySize{ 0 };
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

    bool ObjectStateSerializer::loadFromXMLString( GameObject* pGameObject, string_view xmlString, const ObjectLoadContext& context )
    {
        return loadFromText<XMLSerializer>( pGameObject, xmlString, context );
    }

    bool ObjectStateSerializer::loadFromJSONString( GameObject* pGameObject, string_view jsonString, const ObjectLoadContext& context )
    {
        return loadFromText<JSONSerializer>( pGameObject, jsonString, context );
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
            // 컴포넌트는 id 를 되찾지 못해 되돌리기 뒤 핸들이 끊긴다.
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
        addEntry( pObject, savedId, savedName, bExternalParentAllowed, false );
    }

    void ObjectStateBatch::addLoadedState( GameObject* pObject, uint64 savedId, hashed_string savedName, bool bExternalParentAllowed )
    {
        addEntry( pObject, savedId, savedName, bExternalParentAllowed, true );
    }

    void ObjectStateBatch::addEntry( GameObject* pObject, uint64 savedId, hashed_string savedName, bool bExternalParentAllowed, bool bLoadedState )
    {
        if ( pObject == nullptr )
            return;
        SW_ASSERT( _bFinished == false );

        const uint32 entryIndex = static_cast<uint32>( _listEntry.size() );
        _listEntry.push_back( Entry{ pObject, savedId, savedName, bExternalParentAllowed, bLoadedState } );
        // 같은 id · 이름이 둘이면 먼저 적힌 것이다(문서에 이름이 겹친 엔티티가 있을 수 있다).
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
        //    되돌리면 앞의 것이 뒤의 것 이름을 잠시 쥐고 있어, 이것이 없으면 앞의 것이 영영 `Left_2` 로 남는다. 아직 쥔 오브젝트가 있으면 그대로다.
        for ( const Entry& entry : _listEntry )
        {
            GameObject* pObject = entry._pObject;
            if ( pObject->getManager() == nullptr || pObject->isPendingDestroy() || entry._savedName.empty() || pObject->getName() == entry._savedName )
                continue;
            pObject->setName( entry._savedName ); // 아직 다른 오브젝트가 쥐고 있으면 매니저가 지금 이름(번호)을 그대로 둔다
        }

        // 2) 부착. 모든 오브젝트가 생긴 뒤라 자식이 부모보다 먼저 읽혔어도 부모를 찾는다.
        for ( const Entry& entry : _listEntry )
        {
            resolveEntry( entry );
        }

        // 3) 핸들 PROPERTY. 부착과 같은 규칙으로 저장된 id 를 이 실행의 오브젝트로 옮긴다 — 가리키던 오브젝트가 뒤에 읽혔어도 찾는다.
        for ( const Entry& entry : _listEntry )
        {
            resolveObjectReferences( entry );
        }

        // 4) 값을 다 읽고 이었다 — 컴포넌트가 값을 자원으로 바꾼다(`Component::onPostLoad`, 편집 중에도). 부착 · 핸들이 풀린 뒤라 그것을 읽어도
        //    이 실행의 오브젝트다(언리얼 `PostLoad` 도 패키지의 오브젝트를 모두 읽고 이은 뒤에 온다). 상태 없이 지은 항목(프리팹 스폰)은 스폰이 이미 불렀다.
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._bLoadedState == false || entry._pObject->isPendingDestroy() )
                continue;
            for ( Component* pComp : entry._pObject->getComponents() )
            {
                if ( pComp != nullptr && pComp->isPendingDestroy() == false )
                    pComp->onPostLoad();
            }
        }

        // 5) 읽은 값을 검증한다(`Validate = fn`) — 결과는 `ValidationIssueLog` 로 간다(맵 검사). 값을 고치거나 로드를 멈추지 않는다.
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._bLoadedState == true && entry._pObject->isPendingDestroy() == false )
                (void)ObjectValidation::reportGameObject( *entry._pObject, true );
        }
    }

    void ObjectStateBatch::resolveObjectReferences( const Entry& entry ) const
    {
        GameObject* pObject = entry._pObject;
        if ( pObject->isPendingDestroy() )
            return;

        const hashed_string& handleTypeName = ObjectStateSerializerInternal::getObjectHandleTypeName();
        for ( Component* pComp : pObject->getComponents() )
        {
            const TypeInfo* pTypeInfo = ( pComp != nullptr && pComp->isPendingDestroy() == false ) ? pComp->getTypeInfo() : nullptr;
            if ( pTypeInfo == nullptr )
                continue;
            for ( const PropertyInfo& prop : pTypeInfo->getPropertiesWithBase() )
            {
                // 읽지 않은 칸(Transient)은 지금 실행의 값이다 — 옮기면 살아 있는 참조를 망친다.
                if ( prop._metadata._bTransient == SW_TRUE )
                    continue;
                // 같은 실행의 상태면 런타임 id 그대로가 맞다. 파일 id 는 이 실행에서 다른 오브젝트일 수 있으므로 옮기지 못하는 자리는 비우고 알린다.
                const bool bComponentHandle = ObjectStateSerializerInternal::holdsComponentHandle( prop );
                if ( bComponentHandle == false && prop._bIsContainer == SW_FALSE )
                {
                    if ( prop._typeName == handleTypeName )
                    {
                        GameObjectHandle* pHandle = static_cast<GameObjectHandle*>( prop.getRawPtr( pComp ) );
                        *pHandle                  = resolveObjectReference( *pHandle );
                    }
                    continue;
                }
                bool bRemapped = bComponentHandle == false;
                if ( bRemapped && prop._containerWrapper != nullptr )
                {
                    const NestedContainerInfo shape = prop.getContainerShape();
                    if ( ObjectStateSerializerInternal::holdsObjectHandle( shape ) )
                        bRemapped = remapContainerHandles( prop.getRawPtr( pComp ), shape );
                }
                if ( bRemapped || _idSpace != ObjectIdSpace::Saved )
                    continue;
                SW_LOG_WARNING( "Property '%#::%#' holds object handles a load batch cannot remap (ComponentHandle, or a set of containers) - cleared",
                                pTypeInfo->_fullyQualifiedName.c_str(), prop._name.c_str() );
                if ( prop._bIsContainer == SW_TRUE )
                    prop._containerWrapper->clear( prop.getRawPtr( pComp ) );
                else
                    *static_cast<ComponentHandle*>( prop.getRawPtr( pComp ) ) = ComponentHandle{};
            }
        }
    }

    bool ObjectStateBatch::remapContainerHandles( void* pContainer, const NestedContainerInfo& shape ) const
    {
        if ( pContainer == nullptr || shape._wrapper == nullptr )
            return true;
        const hashed_string& handleTypeName = ObjectStateSerializerInternal::getObjectHandleTypeName();

        ISequenceContainerWrapper* pSequence = shape._wrapper->asSequence();
        if ( pSequence != nullptr )
        {
            const size_t elementCount = pSequence->getSize( pContainer );
            if ( shape._elementNested != nullptr )
            {
                // 원소가 곧 정렬 키인 set 은 원소를 제자리에서 고칠 수 없다 — 컨테이너를 원소로 든 set 은 옮기지 못한다.
                if ( pSequence->allowsInPlaceElementWrite() == false )
                    return ObjectStateSerializerInternal::holdsObjectHandle( *shape._elementNested ) == false;
                for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                {
                    if ( remapContainerHandles( pSequence->getElement( pContainer, elementIndex ), *shape._elementNested ) == false )
                        return false;
                }
                return true;
            }
            if ( shape._elementTypeName != handleTypeName )
                return true;
            if ( pSequence->allowsInPlaceElementWrite() )
            {
                for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                {
                    GameObjectHandle* pHandle = static_cast<GameObjectHandle*>( pSequence->getElement( pContainer, elementIndex ) );
                    *pHandle                  = resolveObjectReference( *pHandle );
                }
                return true;
            }
            // set — 옮긴 값으로 빼고 다시 넣는다(자리는 컨테이너가 정한다).
            vector<GameObjectHandle> listHandle;
            listHandle.reserve( elementCount );
            for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
            {
                listHandle.push_back( resolveObjectReference( *static_cast<const GameObjectHandle*>( pSequence->getElementConst( pContainer, elementIndex ) ) ) );
            }
            pSequence->clear( pContainer );
            for ( size_t elementIndex = 0; elementIndex < listHandle.size(); ++elementIndex )
            {
                const GameObjectHandle handle = listHandle[elementIndex];
                (void)pSequence->appendElement( pContainer, elementIndex, ElementFillDelegate::create( [handle]( void* pElement )
                {
                    *static_cast<GameObjectHandle*>( pElement ) = handle;
                    return true;
                } ) );
            }
            return true;
        }

        IMapContainerWrapper* pMap = shape._wrapper->asMap();
        if ( pMap == nullptr )
            return true;
        // 값부터 제자리에서 옮긴다(핸들 값 · 중첩 컨테이너).
        bool bValueRemapped = true;
        if ( shape._elementNested != nullptr || shape._elementTypeName == handleTypeName )
        {
            pMap->forEachMutable( pContainer, MapForEachMutableDelegate::create( [this, &shape, &bValueRemapped, &handleTypeName]( const void*, void* pValue )
            {
                if ( shape._elementNested != nullptr )
                    bValueRemapped = remapContainerHandles( pValue, *shape._elementNested ) && bValueRemapped;
                else if ( shape._elementTypeName == handleTypeName )
                    *static_cast<GameObjectHandle*>( pValue ) = resolveObjectReference( *static_cast<GameObjectHandle*>( pValue ) );
            } ) );
        }
        if ( bValueRemapped == false || shape._keyTypeName != handleTypeName )
            return bValueRemapped;

        // 키는 정렬 · 해시 자리라 제자리에서 고칠 수 없다 — 값을 밖에 복사해 두고, 비운 뒤 옮긴 키로 다시 넣는다.
        constexpr size_t kAlignment  = 64;
        const size_t     valueStride = ( pMap->getValueSize() + kAlignment - 1 ) / kAlignment * kAlignment;
        const size_t     entryCount  = pMap->getSize( pContainer );
        if ( entryCount == 0 )
            return true;
        uint8* pValueBlock = static_cast<uint8*>( Memory::allocateAligned( valueStride * entryCount, kAlignment ) );
        if ( pValueBlock == nullptr )
            return false;
        vector<GameObjectHandle> listKey;
        listKey.reserve( entryCount );
        bool bCopied = true;
        pMap->forEach( pContainer, MapForEachDelegate::create( [this, pMap, pValueBlock, valueStride, &listKey, &bCopied]( const void* pKey, const void* pValue )
        {
            void* pSlot = pValueBlock + valueStride * listKey.size();
            pMap->defaultConstructValue( pSlot );
            bCopied = pMap->copyValue( pSlot, pValue ) && bCopied;
            listKey.push_back( resolveObjectReference( *static_cast<const GameObjectHandle*>( pKey ) ) );
        } ) );
        if ( bCopied )
        {
            pMap->clear( pContainer );
            for ( size_t entryIndex = 0; entryIndex < listKey.size(); ++entryIndex )
            {
                pMap->insertKeyValue( pContainer, &listKey[entryIndex], pValueBlock + valueStride * entryIndex );
            }
        }
        for ( size_t entryIndex = 0; entryIndex < listKey.size(); ++entryIndex )
        {
            pMap->destroyValue( pValueBlock + valueStride * entryIndex );
        }
        Memory::freeAligned( pValueBlock );
        return bCopied;
    }

    GameObjectHandle ObjectStateBatch::resolveObjectReference( GameObjectHandle savedHandle ) const
    {
        if ( savedHandle.isValid() == false )
            return GameObjectHandle{};
        const GameObject* pReferent = findBySavedId( savedHandle.objectId() );
        if ( pReferent != nullptr )
            return pReferent->getHandle();
        // 같은 실행의 상태면 묶음 밖의 런타임 id 그대로다 — 그 오브젝트가 살아 있으면 그것이고, 사라졌으면 id 가 다시 쓰이지 않으므로 아무것도 아니다.
        if ( _idSpace == ObjectIdSpace::Live )
            return savedHandle;
        // 파일 id 가 이 묶음에 없다 — 이 실행에서 같은 값은 다른 오브젝트다(언리얼의 Instigator 처럼 파일 밖을 가리키는 런타임 참조는 남지 않는다).
        return GameObjectHandle{};
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
            // 소유자 칸이 비었으면 자기다(같은 오브젝트 안의 부착은 소유자 칸을 비워 쓴다).
            if ( ownerName.empty() )
                return entry._pObject;
            // 이름만 남은 다른 오브젝트의 부모다 — 찾지 못한 참조를 다른 id 공간으로 옮겨 적으면 id 를 비운다(`SceneComponent::syncAttachSerializeFields`).
            // **이 묶음의 저장된 이름**에서만 찾는다 — 매니저에서 찾으면 유일하게 바뀐 이름 때문에 다른 오브젝트에 붙는다.
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
            {
                // 프리팹 루트에는 부모가 없다 — 프리팹을 쓰는 쪽은 다른 오브젝트로의 부착을 싣지 않는다(`ObjectSaveOptions::_bOmitExternalParent`).
                SW_LOG_WARNING( "Prefab state of '%#' carries a parent reference to '%#' (id %#, %#) - a prefab root has no parent, the reference is dropped",
                                pObject->getName().c_str(), reference._ownerName.empty() ? "?" : reference._ownerName.c_str(), reference._ownerId,
                                reference._componentKey.c_str() );
                continue;
            }

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
