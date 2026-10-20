#include "pch.h"

#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/UUID/UUID.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/ReflectedValueWalk.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Base/BinaryStream.h"
#include "Engine/Serialization/Base/Serializer.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    namespace
    {
        struct ObjectStateSerializerInternal
        {
            /**
             * @brief 이 오브젝트의 씬 컴포넌트에 붙어 있던 **다른 오브젝트의** 자식 하나입니다. 제자리에서 다시 읽는 동안 분리되었다가 다시 붙습니다.
             * @details 부모는 핸들이 아니라 안정 키(`타입#n`)로 기록한다. 식별 목록 없이 다시 읽으면 컴포넌트 id 가 바뀌고, 이름 기반
             *          부착 필드는 이름을 되돌리는 되돌리기에서 틀린다. 자식은 핸들로 기록한다(그쪽 오브젝트는 그대로 살아 있다).
             */
            struct ChildLink
            {
                TComponentHandle<SceneComponent> _child;
                string                           _parentKey;
            };

            /**
             * @brief 다시 읽기 전에 다른 오브젝트의 자식 연결을 기록합니다.
             * @details 제자리에서 다시 읽으면(되돌리기, 다시 하기, 프리팹으로 되돌리기, 플레이 종료 복원) 컴포넌트를 모두 지우고 새로
             *          만드는데, 씬 컴포넌트의 소멸자가 자식을 분리해서 **다른 오브젝트의 자식들이 루트가 된다.** 로드는 이 오브젝트 **안의**
             *          부착만 다시 붙이므로, 이 기록이 없으면 부모 속성 하나를 고치고 되돌릴 때 자식이 떨어진다.
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
                        const bool bForeignChild = pChild == nullptr || pChild->getOwner() == nullptr || pChild->getOwner() == pGameObject;
                        if ( bForeignChild )
                            continue;
                        outListLink.push_back( ChildLink{ TComponentHandle<SceneComponent>{ pChild }, ComponentStableKey::makeKey( pScene ) } );
                    }
                }
            }

            /**
             * @brief 기록해 둔 자식을 새로 만든 같은 위치의 부모(안정 키)에 다시 붙입니다. 그 위치가 없으면 primary 에, 그것도 없으면 루트로 둡니다.
             * @details 자식의 로컬 트랜스폼은 그대로라 부모가 같은 값으로 돌아오면 월드도 그대로다. 붙이는 함수가 계층 활성도 맞춘다.
             */
            static void restoreChildLinks( GameObject* pGameObject, const vector<ChildLink>& listLink )
            {
                GameObjectManager* pManager = pGameObject->getManager();
                if ( pManager == nullptr )
                    return;
                for ( const ChildLink& link : listLink )
                {
                    SceneComponent* pChild = pManager->resolveComponent( link._child );
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
             * @brief 상태를 읽은 오브젝트를 주변 상태에 다시 맞춥니다. 이름이 바뀌었으면 매니저의 이름 테이블을, 그리고 활성 계층을 맞춥니다.
             * @details XML, JSON, 바이너리 세 로더가 함께 쓴다. 한 포맷만 빠뜨리면 그 포맷으로 되돌린 오브젝트만
             *          이름으로 찾을 수 없게 된다. 부모 연결, 핸들, 컴포넌트 알림(`onPostLoad`)은 여기서 하지 않는다. 배치
             *          (`ObjectStateBatch::finish`)가 모두 읽은 뒤에 처리한다.
             */
            static void finishLoad( GameObject* pGameObject, hashed_string oldName )
            {
                if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
                    pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

                pGameObject->setActive( pGameObject->isActive() );
            }

            /** @brief 상태가 이 오브젝트를 가리킬 때 쓰는 id 입니다. 컨텍스트가 따로 주지 않았으면 복원하는 원래 id 입니다. */
            static uint64 resolveSavedID( const ObjectLoadContext& context )
            {
                if ( context._savedID != 0 )
                    return context._savedID;
                return ( context._pIdentity != nullptr ) ? context._pIdentity->_objectID : 0;
            }

            /**
             * @brief 이름으로 컴포넌트를 만들어 소유자에 붙입니다(역직렬화 팩토리).
             * @details 모르는 타입(게임 모듈이 로드되지 않았거나, 이름을 바꿨는데 별칭이 없는 경우)은 만들지 못한다. 어느 오브젝트의 어느 타입인지 경고하고,
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
             * @brief 모르는 타입의 컴포넌트 원문을 `MissingComponent` 에 보관합니다. 건너뛰면 그대로 저장할 때 그 값이 영영 사라집니다.
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

            /** @brief 원소가 `MissingComponent` 이면 보관한 원문을 반환합니다. 직렬화기가 같은 형식이면 그대로 다시 씁니다. */
            static bool queryMissingComponent( const void* pElement, SerializeContext::OpaqueElementView& outElement )
            {
                const Component* pComp                = static_cast<const Component*>( pElement );
                const bool       bNotMissingComponent = pComp == nullptr || pComp->isPendingDestroy() || pComp->getTypeInfo() != MissingComponent::StaticType();
                if ( bNotMissingComponent )
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

            /** @brief 프로세스 토큰을 새로 정합니다(`UUID` 의 무작위 바이트 8 개). 0 은 "토큰 없음"과 구분되지 않아 피합니다. */
            static uint64 makeProcessToken()
            {
                const UUID uuid  = UUID::generate();
                uint64     token = 0;
                Memory::copy( &token, uuid._arrBytes, sizeof( token ) );
                return ( token != 0 ) ? token : 1;
            }

            static SerializeContext makeGameObjectXMLContext( GameObject* pGameObject, SerializePurpose purpose )
            {
                SerializeContext serializeContext = SerializeContext::deriveFromDefault( purpose );
                serializeContext.setOuterInstance( pGameObject );
                serializeContext.setOwnedPointerFactory( &createOwnedComponent );
                serializeContext.setRuntimeTypeInfoFn( &getComponentRuntimeTypeInfo );
                serializeContext.setOpaqueElementHandlers( &keepMissingComponent, &queryMissingComponent );
                // 현재 타입에 없는 필드는 건너뛰고 읽는다. 세 형식이 같은 규칙이다. 바이너리가 실패하면 컴포넌트 PROPERTY 하나를 지웠을 때 그
                // 컴포넌트를 가진 모든 오브젝트의 세이브와 플레이 스냅샷(핫 리로드 뒤 Stop)을 읽지 못한다. 오브젝트 자기 필드도 같다. 바이너리의
                // 버전 붙은 읽기가 이 값을 같은 버전의 orphan 정책으로 읽는다(`SchemaOrphanPolicy::Ignore`). 버전이 다른 상태는 그래도 거절한다.
                // 버린 필드는 세 형식이 같이 지나는 `runSchemaMigrateStep` 이 이름과 함께 로드마다 한 번 알린다.
                serializeContext.setAllowUnknownProperties( true );
                // 컴포넌트 핸들 텍스트는 두 형태를 읽는다(숫자 런타임 id, 파일 형태의 키). 쓰기는 저장할 때 `registerReferenceWriter` 가 바꾼다. 바이너리는 같은 16 바이트다.
                serializeContext.registerTextHandler(
                    getComponentHandleTypeName(), []( const void* pPtr )
                { return ReferenceWriter::writeRuntimeComponentText( *static_cast<const ComponentHandle*>( pPtr ) ); },
                    []( void* pPtr, string_view text )
                { return readComponentHandleText( pPtr, text ); } );
                return serializeContext;
            }

            /** @brief `GameObjectHandle` 의 타입 이름입니다. 리플렉션 PROPERTY 의 타입 이름과 텍스트 핸들러 이름이 같습니다. */
            static const hashed_string& getObjectHandleTypeName()
            {
                static const hashed_string s_typeName{ "GameObjectHandle" };
                return s_typeName;
            }

            /** @brief @p pObject 의 컴포넌트 가운데 안정 키(`타입#n`)의 해시가 @p keyHash 인 것입니다. 없으면 nullptr 입니다. */
            static Component* findComponentByKeyHash( GameObject* pObject, uint64 keyHash )
            {
                Component* pFound = nullptr;
                pObject->forEachComponent( [&]( Component* pComp )
                {
                    if ( pFound != nullptr || pComp == nullptr )
                        return;
                    if ( hashComponentKey( ComponentStableKey::makeKey( pComp ) ) == keyHash )
                        pFound = pComp;
                } );
                return pFound;
            }

            /** @brief `ComponentHandle` 의 타입 이름입니다. 타입 핸들(`TComponentHandle<T>`)도 파서가 이 이름으로 기록합니다(`PropertyInfo::_handleTargetTypeName` 에 대상 `T`). */
            static const hashed_string& getComponentHandleTypeName()
            {
                static const hashed_string s_typeName{ "ComponentHandle" };
                return s_typeName;
            }

            /** @brief 파일 형태의 컴포넌트 자리(키 해시)에 세우는 표지 비트입니다. 런타임 컴포넌트 id 는 작은 카운터라 이 비트를 쓰지 않는다. */
            static constexpr uint64 kComponentKeyHashMarker = 1ull << 63;

            /**
             * @brief 파일에 적는 컴포넌트 키(`타입#n`)의 해시입니다. 읽은 핸들의 `componentID` 자리에 들어가고, 배치가 대상 오브젝트의 컴포넌트 키 해시와 맞춰 찾습니다.
             * @details 최상위 비트(`kComponentKeyHashMarker`)를 세운다. 그래야 읽는 쪽이 값만 보고 파일 형태와 런타임 id 를 가른다 — 같은 세이브를 같은 실행에서
             *          복원(`Live`)하든 다른 실행에서 읽든(`Saved`) 쓴 형태가 맞는다.
             */
            static uint64 hashComponentKey( string_view key ) { return HashUtil::computeStringHash64( key.data(), key.size(), false ) | kComponentKeyHashMarker; }

            /** @brief 읽은 컴포넌트 자리가 파일 형태(키 해시)인지 가립니다. */
            static bool isComponentKeyHash( uint64 componentID ) { return ( componentID & kComponentKeyHashMarker ) != 0; }

            /**
             * @brief 컴포넌트 핸들 텍스트(`오브젝트:컴포넌트`)를 읽습니다. 컴포넌트 자리가 숫자면 런타임 id 이고, 아니면 파일 형태의 키(`타입#n`)라서 해시로 둡니다.
             * @details 두 형태는 쓴 쪽이 정한다(`ReferenceWriter::writesFileForm`). 읽은 값은 배치가 끝날 때 이 실행의 컴포넌트로 바뀝니다(`ObjectStateBatch::finish`).
             */
            [[nodiscard]] static bool readComponentHandleText( void* pPtr, string_view text )
            {
                const string_view trimmed = StringUtil::trim( text );
                const size_t      sep     = trimmed.find( ':' );
                if ( sep == string_view::npos )
                    return false;
                uint64 objectID{ 0 };
                if ( StringUtil::parseUint64( StringUtil::trim( trimmed.substr( 0, sep ) ), objectID, 10 ) == false )
                    return false;
                const string_view second = StringUtil::trim( trimmed.substr( sep + 1 ) );
                if ( second.empty() )
                    return false;
                uint64 componentID{ 0 };
                if ( StringUtil::parseUint64( second, componentID, 10 ) == false )
                    componentID = hashComponentKey( second );
                *static_cast<ComponentHandle*>( pPtr ) = ComponentHandle::makeOwned( objectID, componentID );
                return true;
            }

            /**
             * @brief 저장할 때 `GameObjectHandle` 값을 저장할 id 로 바꿔 쓰는 텍스트와 바이너리 처리기입니다. 부착과 같은 규칙입니다(`ObjectSaveOptions::getSavedObjectID`).
             * @details 세 형식이 모두 이 처리기를 거친다. 런타임 id 를 그대로 적으면 씬 파일을 다시 열 때 같은 값을 받은 다른 오브젝트를 가리킬 수 있다.
             *          핸들은 내장 타입이라 기본 컨텍스트에 8 바이트 바이너리 처리기가 있다. 텍스트 처리기만 바꾸면 바이너리(쿠킹한 씬, 세이브)는 런타임
             *          id 를 그대로 싣고, 읽는 배치가 그 id 를 파일 id 로 찾지 못해 핸들이 비게 된다. 바이너리도 같은 8 바이트 형태로 바꿔 적는다.
             */
            struct ReferenceWriter
            {
                const ObjectSaveOptions* _pOptions{ nullptr };
                const GameObject*        _pOuter{ nullptr }; ///< 지금 쓰는 오브젝트다. 컴포넌트 핸들의 대상이 이 오브젝트의 것인지(소유자 0) 가린다

                /** @brief 컴포넌트 핸들의 런타임 id 글(`오브젝트:컴포넌트`)입니다. 같은 실행의 스냅샷이 쓰고, 읽기 처리기의 숫자 갈래가 읽는다. */
                static string writeRuntimeComponentText( const ComponentHandle& handle )
                {
                    return sw::to_string( handle.objectID() ) + ":" + sw::to_string( handle.componentID() );
                }

                /**
                 * @brief 컴포넌트 핸들을 **파일 형태**로 적는가입니다. 저장 id 표가 있거나(씬 파일), 프리팹(바깥 참조를 적지 않음)이거나, 세이브이면 그렇고,
                 *        아니면 런타임 id 를 그대로 적습니다(같은 실행의 스냅샷 · 되돌리기 · 복제).
                 * @details 파일 형태는 `{대상 오브젝트의 저장 id, 컴포넌트 안정 키}` 다. 파일 상태에는 컴포넌트 자신의 id 가 없고, 런타임 컴포넌트 id 는 읽는 실행에서
                 *          우연히 같은 값을 받은 다른 컴포넌트를 가리킨다. 부착(`SceneAttachReference`)과 같은 방식이다. 세이브는 다른 실행에서 읽을 수 있어 늘 파일 형태이고,
                 *          같은 실행에서 복원해도 읽는 쪽이 키 해시의 표지 비트로 형태를 알아본다(`isComponentKeyHash`).
                 */
                bool writesFileForm() const
                {
                    return _pOptions->_pSavedIDMap != nullptr || _pOptions->_bOmitExternalParent || _pOptions->getPurpose() == SerializePurpose::SaveGame;
                }

                /** @brief 파일 형태의 컴포넌트 참조 하나입니다. `_key` 가 비면 "대상 없음" 입니다. */
                struct ComponentReference
                {
                    uint64 _objectID{ 0 }; ///< 0 이면 이 오브젝트 자신(부착의 "소유자 필드가 비면 자기 자신" 과 같은 규칙)
                    string _key{};
                };

                /**
                 * @brief @p pValue 의 컴포넌트 핸들이 가리키는 컴포넌트를 파일 형태로 바꿉니다.
                 * @details 대상이 없거나(사라졌거나 비었거나), 이 파일에 없는 다른 오브젝트의 컴포넌트면 "대상 없음" 입니다. 프리팹은 바깥 오브젝트를 적지 않습니다.
                 */
                ComponentReference computeComponentReference( const void* pValue ) const
                {
                    ComponentReference       reference;
                    const ComponentHandle&   handle   = *static_cast<const ComponentHandle*>( pValue );
                    const GameObjectManager* pManager = _pOuter != nullptr ? _pOuter->getManager() : nullptr;
                    const Component*         pTarget  = ( pManager != nullptr && handle.isValid() ) ? pManager->resolveComponent( handle ) : nullptr;
                    if ( pTarget == nullptr || pTarget->getOwner() == nullptr )
                        return reference;
                    string key = ComponentStableKey::makeKey( pTarget );
                    if ( key.empty() )
                        return reference;
                    if ( pTarget->getOwner() != _pOuter )
                    {
                        if ( _pOptions->_bOmitExternalParent )
                            return reference;
                        reference._objectID = _pOptions->getSavedObjectID( handle.objectID() );
                        if ( reference._objectID == 0 )
                            return reference;
                    }
                    reference._key = std::move( key );
                    return reference;
                }

                string writeComponent( const void* pValue ) const
                {
                    if ( writesFileForm() == false )
                        return writeRuntimeComponentText( *static_cast<const ComponentHandle*>( pValue ) );
                    const ComponentReference reference = computeComponentReference( pValue );
                    return reference._key.empty() ? string{ "0:0" } : sw::to_string( reference._objectID ) + ":" + reference._key;
                }

                /** @brief 기본 바이너리 처리기와 같은 형태(핸들 하나, 16 바이트)로 씁니다. 파일 형태에서는 컴포넌트 자리에 키 해시가 들어갑니다. */
                void writeComponentBinary( const void* pValue, vector<uint8>& outListBuffer ) const
                {
                    ComponentHandle written = *static_cast<const ComponentHandle*>( pValue );
                    if ( writesFileForm() )
                    {
                        const ComponentReference reference = computeComponentReference( pValue );
                        written                            = reference._key.empty() ? ComponentHandle{} : ComponentHandle::makeOwned( reference._objectID, hashComponentKey( reference._key ) );
                    }
                    const uint8* pByte = reinterpret_cast<const uint8*>( &written );
                    outListBuffer.insert( outListBuffer.end(), pByte, pByte + sizeof( ComponentHandle ) );
                }

                uint64 computeSavedID( const void* pValue ) const
                {
                    const GameObjectHandle& handle = *static_cast<const GameObjectHandle*>( pValue );
                    const bool              bOmit  = handle.isValid() == false || _pOptions->_bOmitExternalParent;
                    return bOmit ? 0 : _pOptions->getSavedObjectID( handle.objectID() );
                }

                string write( const void* pValue ) const { return sw::to_string( computeSavedID( pValue ) ); }

                /** @brief 기본 바이너리 처리기와 같은 형태(핸들 하나의 바이트)로 저장할 id 를 씁니다. 읽기는 기본 처리기를 그대로 씁니다. */
                void writeBinary( const void* pValue, vector<uint8>& outListBuffer ) const
                {
                    const GameObjectHandle savedHandle = GameObjectHandle::make( computeSavedID( pValue ) );
                    const uint8*           pByte       = reinterpret_cast<const uint8*>( &savedHandle );
                    outListBuffer.insert( outListBuffer.end(), pByte, pByte + sizeof( GameObjectHandle ) );
                }
            };

            /** @brief 오브젝트 상태를 쓰는 컨텍스트에 핸들 텍스트와 바이너리 처리기를 등록합니다. 읽기는 기본 처리기를 그대로 쓰며, 옮기는 일은 배치가 모두 읽은 뒤 합니다(`ObjectStateBatch::finish`). */
            static void registerReferenceWriter( SerializeContext& serializeContext, const ReferenceWriter& writer )
            {
                // 컴포넌트 핸들: 읽기는 `makeGameObjectXMLContext` 가 이미 두 형태(숫자 · 키)를 읽는 처리기로 바꿔 두었다.
                const SerializeContext::TextReadFn* pComponentReader = serializeContext.findTextReader( getComponentHandleTypeName() );
                if ( pComponentReader != nullptr )
                    serializeContext.registerTextHandler( getComponentHandleTypeName(), SW_DELEGATE_METHOD( SerializeContext::TextWriteFn, &ReferenceWriter::writeComponent, &writer ),
                                                          *pComponentReader );
                const SerializeContext::BinaryReadFn* pComponentBinaryReader = serializeContext.findBinaryReader( getComponentHandleTypeName() );
                if ( pComponentBinaryReader != nullptr )
                    serializeContext.registerBinaryHandler( getComponentHandleTypeName(),
                                                            SW_DELEGATE_METHOD( SerializeContext::BinaryWriteFn, &ReferenceWriter::writeComponentBinary, &writer ), *pComponentBinaryReader );

                const SerializeContext::TextReadFn* pReader = serializeContext.findTextReader( getObjectHandleTypeName() );
                if ( pReader == nullptr )
                    return;
                serializeContext.registerTextHandler( getObjectHandleTypeName(), SW_DELEGATE_METHOD( SerializeContext::TextWriteFn, &ReferenceWriter::write, &writer ),
                                                      *pReader );
                const SerializeContext::BinaryReadFn* pBinaryReader = serializeContext.findBinaryReader( getObjectHandleTypeName() );
                if ( pBinaryReader != nullptr )
                    serializeContext.registerBinaryHandler( getObjectHandleTypeName(),
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

        // 텍스트로 저장하는 경로(씬과 프리팹 저작)는 쓰기 전에 검증한다. 결과만 남기고 저장은 그대로 한다. 바이너리(플레이, 되돌리기 스냅샷)는 검증하지 않는다.
        (void)ObjectValidation::reportGameObject( *pGameObject, true );

        SerializeContext                                     serializeContext = ObjectStateSerializerInternal::makeGameObjectXMLContext( const_cast<GameObject*>( pGameObject ), options.getPurpose() );
        const ObjectStateSerializerInternal::ReferenceWriter referenceWriter{ &options, pGameObject };
        ObjectStateSerializerInternal::registerReferenceWriter( serializeContext, referenceWriter );
        return TSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, serializeContext );
    }

    template <typename DeserializeStateFunc>
    bool ObjectStateSerializer::loadStateInPlace( GameObject* pGameObject, const ObjectLoadContext& context, DeserializeStateFunc&& deserializeState )
    {
        // 제자리 읽기는 컴포넌트를 모두 비우고 다시 만든다. 컴포넌트 틱 중에는 할 수 없다(다른 워커가 그 컴포넌트를 틱한다). 결과를 바로 반환해야
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

        // 읽기 전 상태를 기록해 둔다. 실패하면 그 상태로 되돌린다. 컴포넌트가 없는 오브젝트(새로 만든 것, 씬 로드, 복제)는 기록할 것이 없다.
        vector<uint8>  previousBytes;
        ObjectIdentity previousIdentity;
        if ( context._bRestorePreviousOnFailure && pGameObject->getComponents().empty() == false )
        {
            previousIdentity = captureIdentity( pGameObject );
            if ( saveToBinaryBuffer( pGameObject, previousBytes, ObjectSaveOptions{ SerializePurpose::Snapshot } ) == false )
                previousBytes.clear();
        }
        pGameObject->clearComponents();

        const GameObject::ComponentIDRestoreScope restoreScope( pGameObject, context._pIdentity );
        const SerializeContext                    serializeContext = ObjectStateSerializerInternal::makeGameObjectXMLContext( pGameObject, context.getPurpose() );
        uint32                                    version{ 0 };
        const bool                                bLoaded = deserializeState( version, serializeContext );
        if ( bLoaded )
        {
            // 상태에 적힌 이름이다. 매니저가 유일하게 바꾸기(`finishLoad`) 전에 잡는다. 같은 배치의 이름만 남은 참조가 이 이름으로 찾는다.
            const hashed_string savedName = pGameObject->getName();
            ObjectStateSerializerInternal::finishLoad( pGameObject, oldName );
            const uint64 savedID = ObjectStateSerializerInternal::resolveSavedID( context );
            if ( context._pBatch != nullptr )
            {
                context._pBatch->addLoadedState( pGameObject, savedID, savedName, context.getPurpose(), context._bExternalParentAllowed );
            }
            else
            {
                // 혼자 읽는 상태도 같은 규칙으로 연결한다. 같은 실행의 상태라 다른 오브젝트는 매니저의 런타임 id 로 찾는다.
                ObjectStateBatch single( ObjectIDSpace::Live );
                single.addLoadedState( pGameObject, savedID, savedName, context.getPurpose(), context._bExternalParentAllowed );
                // `Live` 배치는 런타임 id 가 맞아 옮기지 못한 핸들을 실패로 치지 않는다. 늘 true 다.
                [[maybe_unused]] const bool bLinked = single.finish();
                SW_ASSERT( bLinked );
            }
        }
        else if ( previousBytes.empty() == false )
        {
            SW_LOG_WARNING( "State of '%#' could not be read - it is restored to what it was before the load", oldName.c_str() );
            ObjectLoadContext restoreContext{ SerializePurpose::Snapshot };
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

        return loadStateInPlace( pGameObject, context, [&]( uint32& outVersion, const SerializeContext& serializeContext )
        {
            return TSerializer::deserializeVersioned( outVersion, pGameObject, *pTypeInfo, text, kObjectReflectedSchemaVersion, nullptr, serializeContext );
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

        // 부모는 따로 적지 않는다. 씬 컴포넌트의 부착 필드(`_attachOwnerID`, `_attachComponent`)로 상태 안에 들어 있다.
        // 본문 크기를 앞에 둔다. 세이브 게임은 오브젝트를 이어 붙여 놓고 하나씩 끊어 읽는다.
        const size_t sizeHeaderPos = writer.getOffset();
        writer.write( static_cast<uint32>( 0 ) );

        const size_t                                         bodyStart        = writer.getOffset();
        SerializeContext                                     serializeContext = ObjectStateSerializerInternal::makeGameObjectXMLContext( const_cast<GameObject*>( pGameObject ), options.getPurpose() );
        const ObjectStateSerializerInternal::ReferenceWriter referenceWriter{ &options, pGameObject };
        ObjectStateSerializerInternal::registerReferenceWriter( serializeContext, referenceWriter );
        if ( BinarySerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, outBuffer, serializeContext ) == false )
            return false;
        writer.writeAt( sizeHeaderPos, static_cast<uint32>( writer.getOffset() - bodyStart ) );

        return true;
    }

    size_t ObjectStateSerializer::loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, const ObjectLoadContext& context )
    {
        const bool bInvalidLoadArguments = pGameObject == nullptr || pData == nullptr || size == 0;
        if ( bInvalidLoadArguments )
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

        const bool bLoaded = loadStateInPlace( pGameObject, context, [&]( uint32& outVersion, const SerializeContext& serializeContext )
        {
            return BinarySerializer::deserializeVersioned( outVersion, pGameObject, *pTypeInfo, pData + bodyStart, bodySize,
                                                           kObjectReflectedSchemaVersion, nullptr, serializeContext );
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

        identity._objectID = pGameObject->getObjectID();
        identity._listComponent.reserve( pGameObject->getComponents().size() );
        for ( const Component* pComp : pGameObject->getComponents() )
        {
            if ( pComp == nullptr || pComp->isPendingDestroy() )
                continue;
            // 타입 FQN 으로 적는다. 복원할 때(`takeRestoredComponentID`) 새 컴포넌트의 타입 FQN 과 비교한다. 컴포넌트 이름을 적으면 이름을 붙인
            // 컴포넌트는 id 를 되찾지 못해 되돌리기 뒤 핸들이 끊기고, 짧은 타입 이름을 적으면 이름이 같은 다른 타입이 id 를 가져간다.
            // 타입 정보가 없으면(모듈이 내려감) 빈 값이고, 복원은 빈 값을 맞추지 않는다(그 컴포넌트는 새 id 를 받는다).
            const TypeInfo* pTypeInfo = pComp->findCachedTypeInfo();
            identity._listComponent.push_back( ObjectIdentity::ComponentEntry{ pTypeInfo != nullptr ? pTypeInfo->_fullyQualifiedName : hashed_string{}, pComp->getComponentID(),
                                                                               ComponentStableKey::makeKey( pComp ) } );
        }
        return identity;
    }

    void ObjectStateSerializer::writeIdentity( const ObjectIdentity& identity, vector<uint8>& outBuffer )
    {
        BinaryStreamWriter writer( outBuffer );
        writer.write( identity._objectID );
        writer.write( static_cast<uint32>( identity._listComponent.size() ) );
        for ( const ObjectIdentity::ComponentEntry& entry : identity._listComponent )
        {
            writer.writeString( entry._typeFqn.c_str() );
            writer.write( entry._componentID );
            writer.writeString( entry._stableKey.c_str() );
        }
    }

    size_t ObjectStateSerializer::readIdentity( const uint8* pData, size_t size, ObjectIdentity& outIdentity )
    {
        outIdentity = ObjectIdentity{};
        if ( pData == nullptr || size == 0 )
            return 0;

        BinaryStreamReader reader( pData, size );
        uint32             componentCount{ 0 };
        if ( reader.read( outIdentity._objectID ) == false || reader.read( componentCount ) == false )
            return 0;

        // 항목 하나는 적어도 이름 길이(4)와 ID(8)와 키 길이(4)를 합친 바이트다. 남은 바이트로 담을 수 없는 개수는 망가진 데이터다.
        // 그대로 `reserve` 하면 그 한 줄이 먼저 터진다(`GameInstanceBase::deserializeSceneObjects` 가 같은 이유로 같은 계산을 한다).
        constexpr size_t kMinBytesPerEntry = sizeof( uint32 ) + sizeof( uint64 ) + sizeof( uint32 );
        if ( componentCount > ( size - reader.getOffset() ) / kMinBytesPerEntry )
            return 0;

        outIdentity._listComponent.reserve( componentCount );
        for ( uint32 entryIndex = 0; entryIndex < componentCount; ++entryIndex )
        {
            string                         typeName;
            ObjectIdentity::ComponentEntry entry;
            const bool                     bEntryRead = reader.readString( typeName ) && reader.read( entry._componentID ) && reader.readString( entry._stableKey );
            if ( bEntryRead == false )
                return 0;
            entry._typeFqn = hashed_string( typeName.data(), static_cast<uint32>( typeName.size() ) );
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
    // ObjectStateBatch: 모두 읽은 뒤 부착을 한 번에 연결한다
    // ======================================================================

    ObjectStateBatch::ObjectStateBatch( ObjectIDSpace idSpace )
        : _listEntry{}
        , _mapSavedIDToObject{}
        , _mapSavedNameToEntry{}
        , _idSpace{ idSpace }
        , _bFinished{ false }
        , _bReferencesResolved{ true }
    {
    }

    ObjectStateBatch::~ObjectStateBatch()
    {
        // 기록만 하고 연결하지 않으면 그 오브젝트들은 루트로 남는다. 부른 쪽이 `finish` 를 빠뜨린 것이다.
        SW_ASSERT( _listEntry.empty() || _bFinished );
    }

    void ObjectStateBatch::add( GameObject* pObject, uint64 savedID, hashed_string savedName, bool bExternalParentAllowed )
    {
        addEntry( pObject, savedID, savedName, bExternalParentAllowed, std::nullopt );
    }

    void ObjectStateBatch::addLoadedState( GameObject* pObject, uint64 savedID, hashed_string savedName, SerializePurpose purpose, bool bExternalParentAllowed )
    {
        addEntry( pObject, savedID, savedName, bExternalParentAllowed, purpose );
    }

    void ObjectStateBatch::addEntry( GameObject* pObject, uint64 savedID, hashed_string savedName, bool bExternalParentAllowed,
                                     std::optional<SerializePurpose> loadedPurpose )
    {
        if ( pObject == nullptr )
            return;
        SW_ASSERT( _bFinished == false );

        const uint32 entryIndex = static_cast<uint32>( _listEntry.size() );
        _listEntry.push_back( Entry{ pObject, savedID, savedName, bExternalParentAllowed, loadedPurpose } );
        // 같은 id 나 이름이 둘이면 먼저 기록된 것을 쓴다(문서에 이름이 겹친 엔티티가 있을 수 있다).
        if ( savedID != 0 )
            _mapSavedIDToObject.emplace( savedID, pObject );
        if ( savedName.empty() == false )
            _mapSavedNameToEntry.emplace( savedName, entryIndex );
    }

    bool ObjectStateBatch::finish()
    {
        if ( _bFinished )
            return _bReferencesResolved;
        _bFinished = true;

        // 1) 이름. 읽는 동안 다른 오브젝트가 잠시 차지하고 있던 저장된 이름이 이제 비었으면 되찾는다. 플레이 중 이름을 서로 바꾼 오브젝트를
        //    되돌리면 앞의 것이 뒤의 것 이름을 잠시 차지하고 있어서, 이 단계가 없으면 앞의 것이 영영 `Left_2` 로 남는다. 아직 차지한 오브젝트가 있으면 그대로 둔다.
        for ( const Entry& entry : _listEntry )
        {
            GameObject* pObject     = entry._pObject;
            const bool  bSkipRename = pObject->getManager() == nullptr || pObject->isPendingDestroy() || entry._savedName.empty() || pObject->getName() == entry._savedName;
            if ( bSkipRename )
                continue;
            pObject->setName( entry._savedName ); // 아직 다른 오브젝트가 사용 중이면 매니저가 현재 이름(번호)을 그대로 둔다
        }

        // 2) 부착. 모든 오브젝트가 생긴 뒤라 자식이 부모보다 먼저 읽혔어도 부모를 찾는다.
        for ( const Entry& entry : _listEntry )
        {
            resolveEntry( entry );
        }

        // 3) 핸들 PROPERTY. 부착과 같은 규칙으로 저장된 id 를 이 실행의 오브젝트로 옮긴다. 가리키던 오브젝트가 나중에 읽혔어도 찾는다.
        for ( const Entry& entry : _listEntry )
        {
            _bReferencesResolved = resolveObjectReferences( entry ) && _bReferencesResolved;
        }

        // 4) 값을 다 읽고 연결했다. 컴포넌트가 값을 리소스로 바꾼다(`Component::onPostLoad`, 편집 중에도). 부착과 핸들이 해석된 뒤라 그것을 읽어도
        //    이 실행의 오브젝트다(언리얼의 `PostLoad` 도 패키지의 오브젝트를 모두 읽고 연결한 뒤에 온다). 상태 없이 만든 항목(프리팹 스폰)은 스폰이 이미 불렀다.
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._loadedPurpose.has_value() == false || entry._pObject->isPendingDestroy() )
                continue;
            for ( Component* pComp : entry._pObject->getComponents() )
            {
                if ( pComp != nullptr && pComp->isPendingDestroy() == false )
                    pComp->onPostLoad();
            }
        }

        // 5) 읽은 값을 검증한다(`Validate = fn`). 결과는 `ValidationIssueLog` 로 간다(맵 검사). 값을 고치거나 로드를 멈추지 않는다.
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._loadedPurpose.has_value() && entry._pObject->isPendingDestroy() == false )
                (void)ObjectValidation::reportGameObject( *entry._pObject, true );
        }
        return _bReferencesResolved;
    }

    bool ObjectStateBatch::resolveObjectReferences( const Entry& entry ) const
    {
        GameObject* pObject = entry._pObject;
        // 상태를 읽지 않은 항목(`add`)의 값은 이 배치가 읽은 것이 아니다. 옮기면 살아 있는 참조를 망친다.
        if ( pObject->isPendingDestroy() || entry._loadedPurpose.has_value() == false )
            return true;

        // 읽지 않은 필드(Transient, 저작 읽기의 RuntimeState)는 현재 실행의 값이다 — 읽기와 같은 판정(`shouldSerializeProperty`)으로 거른다.
        const SerializeContext& context = SerializeContext::getDefault( *entry._loadedPurpose );
        auto                    filter  = [&context]( const TypeInfo& owner, const PropertyInfo& prop )
        { return SerializerUtil::shouldSerializeProperty( owner, prop, context ); };
        const TypeRegistry& registry = context.getTypeRegistry();
        auto                remap    = [this]( const GameObjectHandle& savedHandle, const PropertyInfo& )
        { return resolveObjectReference( savedHandle ); };
        ReflectedValueWalk<GameObjectHandle, decltype( filter ), decltype( remap )> objectWalk( registry, ObjectStateSerializerInternal::getObjectHandleTypeName(), filter, remap );

        // 컴포넌트 핸들은 오브젝트 핸들과 같은 깊이를 같은 방식으로 내려가되, 잎이 든 필드의 대상 타입을 확인한다.
        bool bInvalid       = false;
        auto remapComponent = [this, &entry, &registry, &bInvalid]( const ComponentHandle& savedHandle, const PropertyInfo& leafProperty )
        { return resolveComponentReference( entry, registry, savedHandle, leafProperty, bInvalid ); };
        ReflectedValueWalk<ComponentHandle, decltype( filter ), decltype( remapComponent )> componentWalk( registry, ObjectStateSerializerInternal::getComponentHandleTypeName(), filter,
                                                                                                           remapComponent );

        const auto walkAllProperties = [this, pObject]( auto& walk, const utf8* pLeafKind )
        {
            bool bAllWalked = true;
            for ( Component* pComp : pObject->getComponents() )
            {
                const TypeInfo* pTypeInfo = ( pComp != nullptr && pComp->isPendingDestroy() == false ) ? pComp->getTypeInfo() : nullptr;
                if ( pTypeInfo == nullptr || walk.holdsLeaf( *pTypeInfo ) == false )
                    continue;
                for ( const PropertyInfo& prop : pTypeInfo->getPropertiesWithBase() )
                {
                    if ( walk.walkProperty( pComp, *pTypeInfo, prop ) )
                        continue;
                    // 같은 실행의 상태면 런타임 id 그대로가 맞다. 파일 id 는 이 실행에서 다른 오브젝트일 수 있으므로 옮기지 못한 것은 로드 오류다.
                    // 비우지 않는다 — 값이 조용히 사라지는 폴백이다.
                    if ( _idSpace != ObjectIDSpace::Saved )
                        continue;
                    SW_LOG_ERROR( "Property '%#::%#' of '%#' holds %# a load batch cannot remap (inside a set element or a map key that is a struct or "
                                  "container, or a copy failed) - the load fails",
                                  pTypeInfo->_fullyQualifiedName.c_str(), prop._name.c_str(), pObject->getName().c_str(), pLeafKind );
                    bAllWalked = false;
                }
            }
            return bAllWalked;
        };
        bool bAllRemapped = walkAllProperties( objectWalk, "object handles" );
        bAllRemapped      = walkAllProperties( componentWalk, "component handles" ) && bAllRemapped;
        // 대상이 있는데 대상 타입이 아닌 핸들(`resolveComponentReference` 가 이유를 로그에 남겼다)은 어느 id 공간이든 로드 오류다.
        return bAllRemapped && bInvalid == false;
    }

    ComponentHandle ObjectStateBatch::resolveComponentReference( const Entry& entry, const TypeRegistry& registry, ComponentHandle savedHandle, const PropertyInfo& leafProperty,
                                                                 bool& outInvalid ) const
    {
        if ( savedHandle.componentID() == 0 )
            return ComponentHandle{}; // 대상 없음(`0:0`)

        // 형태는 값이 말한다(`isComponentKeyHash`): 파일 형태는 `{오브젝트의 저장 id, 키 해시}` 이고, 오브젝트 자리가 0 이면 이 오브젝트 자신이다
        // (부착의 "소유자 필드가 비면 자기 자신" 과 같은 규칙). 런타임 형태는 이 실행의 id 라서 같은 실행의 상태(Live)에서만 뜻이 있다.
        Component* pTarget = nullptr;
        if ( ObjectStateSerializerInternal::isComponentKeyHash( savedHandle.componentID() ) )
        {
            GameObject* pTargetObject = savedHandle.objectID() == 0 ? entry._pObject : findBySavedID( savedHandle.objectID() );
            if ( pTargetObject == nullptr )
                return ComponentHandle{}; // 파일 밖(또는 이 배치에 없는) 오브젝트다. 약한 참조라서 사라진 대상은 정상 값이다
            pTarget = ObjectStateSerializerInternal::findComponentByKeyHash( pTargetObject, savedHandle.componentID() );
            if ( pTarget == nullptr )
            {
                SW_LOG_WARNING( "Handle '%#' of '%#' points at a component of '%#' that no longer exists (key hash %#) - the handle is cleared", leafProperty._name.c_str(),
                                entry._pObject->getName().c_str(), pTargetObject->getName().c_str(), savedHandle.componentID() );
                return ComponentHandle{};
            }
        }
        else if ( _idSpace == ObjectIDSpace::Saved )
        {
            // 런타임 id 를 파일 id 공간으로 읽을 수 없다. 이 실행의 컴포넌트 id 가 우연히 같은 값을 받은 다른 컴포넌트를 가리키므로 비우고 조용히 넘기지 않는다.
            SW_LOG_ERROR( "Handle '%#' of '%#' holds a runtime component id but is read as a saved file - write it with a saved id table, as a prefab or as a save game "
                          "(object %#, component %#) - the load fails",
                          leafProperty._name.c_str(), entry._pObject->getName().c_str(), savedHandle.objectID(), savedHandle.componentID() );
            outInvalid = true;
            return ComponentHandle{};
        }
        else
        {
            const GameObjectManager* pManager = entry._pObject->getManager();
            pTarget                           = pManager != nullptr ? pManager->resolveComponent( savedHandle ) : nullptr;
            if ( pTarget == nullptr )
                return savedHandle; // 사라진 대상: 같은 실행에서는 id 가 다시 쓰이지 않으므로 그대로 두면 찾을 때 nullptr 이다
        }

        // 대상 타입 확인. 타입 없는 핸들(대상 타입이 비었다)이나 모르는 타입은 확인할 수 없으므로 오류다 — 파서가 막는 모양이라 손으로 만든 타입 정보다.
        const TypeInfo* pTargetType = leafProperty._handleTargetTypeName.empty() ? nullptr : registry.findType( leafProperty._handleTargetTypeName );
        const TypeInfo* pActualType = pTarget->getTypeInfo();
        const bool      bTypesKnown = pTargetType != nullptr && pActualType != nullptr;
        if ( bTypesKnown == false || pActualType->isDerivedFrom( pTargetType ) == false )
        {
            SW_LOG_ERROR( "Handle '%#' of '%#' must point at a '%#' but points at a '%#' (component '%#') - the load fails", leafProperty._name.c_str(),
                          entry._pObject->getName().c_str(), leafProperty._handleTargetTypeName.empty() ? "?" : leafProperty._handleTargetTypeName.c_str(),
                          pActualType != nullptr ? pActualType->_fullyQualifiedName.c_str() : "?", ComponentStableKey::makeKey( pTarget ).c_str() );
            outInvalid = true;
            return ComponentHandle{};
        }
        return pTarget->getHandle();
    }

    GameObjectHandle ObjectStateBatch::resolveObjectReference( GameObjectHandle savedHandle ) const
    {
        if ( savedHandle.isValid() == false )
            return GameObjectHandle{};
        const GameObject* pReferent = findBySavedID( savedHandle.objectID() );
        if ( pReferent != nullptr )
            return pReferent->getHandle();
        // 같은 실행의 상태면 배치 밖의 런타임 id 그대로다. 그 오브젝트가 살아 있으면 그것이고, 사라졌으면 id 가 다시 쓰이지 않으므로 아무것도 아니다.
        if ( _idSpace == ObjectIDSpace::Live )
            return savedHandle;
        // 파일 id 가 이 배치에 없다. 이 실행에서 같은 값은 다른 오브젝트다(언리얼의 Instigator 처럼 파일 밖을 가리키는 런타임 참조는 남지 않는다).
        return GameObjectHandle{};
    }

    GameObject* ObjectStateBatch::findBySavedID( uint64 savedID ) const
    {
        const auto mapIt = _mapSavedIDToObject.find( savedID );
        return ( mapIt != _mapSavedIDToObject.end() ) ? mapIt->second : nullptr;
    }

    GameObject* ObjectStateBatch::findBySavedName( hashed_string savedName ) const
    {
        const auto mapIt = _mapSavedNameToEntry.find( savedName );
        return ( mapIt != _mapSavedNameToEntry.end() ) ? _listEntry[mapIt->second]._pObject : nullptr;
    }

    GameObject* ObjectStateBatch::findAttachOwner( const Entry& entry, hashed_string ownerName, uint64 ownerID ) const
    {
        if ( ownerID == 0 )
        {
            // 소유자 필드가 비었으면 자기 자신이다(같은 오브젝트 안의 부착은 소유자 필드를 비워 쓴다).
            if ( ownerName.empty() )
                return entry._pObject;
            // 이름만 남은 다른 오브젝트의 부모다. 찾지 못한 참조를 다른 id 공간으로 옮겨 적으면 id 를 비운다(`SceneComponent::syncAttachSerializeFields`).
            // **이 배치의 저장된 이름**에서만 찾는다. 매니저에서 찾으면 유일하게 바뀐 이름 때문에 다른 오브젝트에 붙는다.
            return findBySavedName( ownerName );
        }
        if ( ownerID == entry._savedID )
            return entry._pObject;

        GameObject* pOwner = findBySavedID( ownerID );
        if ( pOwner != nullptr )
            return pOwner;
        // 같은 실행의 상태면 배치 밖(이미 살아 있는 부모, 되돌리기, 복제)도 런타임 id 로 찾는다. 파일 id 는 이 실행의 id 와 우연히 같을 수 있어 찾지 않는다.
        if ( _idSpace == ObjectIDSpace::Live && entry._pObject->getManager() != nullptr )
            return entry._pObject->getManager()->findGameObjectByID( ownerID );
        return nullptr;
    }

    void ObjectStateBatch::resolveEntry( const Entry& entry ) const
    {
        GameObject* pObject = entry._pObject;
        if ( pObject->isPendingDestroy() )
            return;

        for ( Component* pComp : pObject->getComponents() )
        {
            const bool bSkipNonSceneChild = pComp == nullptr || pComp->isPendingDestroy() || pComp->isSceneComponent() == false;
            if ( bSkipNonSceneChild )
                continue;
            SceneComponent*      pScene    = static_cast<SceneComponent*>( pComp );
            SceneAttachReference reference = pScene->getLoadedAttachReference();
            if ( reference._componentKey.empty() )
                continue; // 루트
            reference._idSpace = _idSpace;

            GameObject* pOwner    = findAttachOwner( entry, reference._ownerName, reference._ownerID );
            const bool  bExternal = pOwner != pObject;
            if ( bExternal && entry._bExternalParentAllowed == false )
            {
                // 프리팹 루트에는 부모가 없다. 프리팹을 쓰는 쪽은 다른 오브젝트로의 부착을 싣지 않는다(`ObjectSaveOptions::_bOmitExternalParent`).
                SW_LOG_WARNING( "Prefab state of '%#' carries a parent reference to '%#' (id %#, %#) - a prefab root has no parent, the reference is dropped",
                                pObject->getName().c_str(), reference._ownerName.empty() ? "?" : reference._ownerName.c_str(), reference._ownerID,
                                reference._componentKey.c_str() );
                continue;
            }

            Component*      pParentComp     = ( pOwner != nullptr ) ? ComponentStableKey::findComponent( pOwner, reference._componentKey.c_str() ) : nullptr;
            SceneComponent* pParent         = ( pParentComp != nullptr && pParentComp->isSceneComponent() ) ? static_cast<SceneComponent*>( pParentComp ) : nullptr;
            const bool      bParentAttached = pParent != nullptr && pParent != pScene && pScene->attachToComponent( pParent );
            if ( bParentAttached )
                continue;

            // 찾지 못했다. 지우지 않고 남긴다. 다음 저장이 그대로 다시 쓰고, 부모가 돌아오면(프리팹을 되찾은 경우) 다음 로드가 붙인다.
            SW_LOG_WARNING( "'%#' keeps its parent reference to '%#' (id %#, %#) - the parent is not loaded", pObject->getName().c_str(),
                            reference._ownerName.empty() ? "self" : reference._ownerName.c_str(), reference._ownerID, reference._componentKey.c_str() );
            pScene->keepUnresolvedAttach( reference );
        }
    }

} // namespace sw
