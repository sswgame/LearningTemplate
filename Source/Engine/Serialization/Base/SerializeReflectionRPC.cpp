/**
 * @file SerializeReflectionRPC.cpp
 * @brief RPC 인자 마샬링입니다. `ReflectionRPC` 의 구현입니다.
 *
 * @details 이 파일이 **Serialization 에 있는 이유**: 내용 모두가 "인자를 바이트로 싣고 다시
 *          꺼내는" 일이고, 그 규약은 `SerializeContext` 의 핸들러 표와 `BinarySerializer` 가
 *          정합니다. 선언(`Reflection/ReflectionRPC.h`)은 리플렉션이 노출하는 API 로 남습니다.
 *
 *          같은 규칙이 `SerializeReflectAny.cpp` 에도 적용됩니다.
 *          **리플렉션 타입의 인코딩은 Serialization 이 갖습니다.** 반대로 두면 Reflection 과
 *          Serialization 이 서로를 참조해 티어 순서를 정할 수 없게 됩니다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionRPC.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{
    namespace
    {
        struct ReflectionRPCInternal
        {
            static hashed_string resolveBuiltinHandlerKey( const hashed_string& typeHash, const SerializeContext& serializeContext )
            {
                if ( serializeContext.findBinaryWriter( typeHash ) != nullptr )
                    return typeHash;
                const TypeInfo* pInfo = engine::getTypeRegistry().findType( typeHash );
                if ( pInfo != nullptr )
                {
                    if ( pInfo->_name.empty() == false && serializeContext.findBinaryWriter( pInfo->_name ) != nullptr )
                        return pInfo->_name;
                }
                return typeHash;
            }

            static bool packOneArg( vector<uint8>& outBytes, string_view typeName, const TaskValue& value,
                                    const SerializeContext& serializeContext )
            {
                const hashed_string typeHash( typeName.data(), static_cast<uint32>( typeName.size() ) );
                const hashed_string handlerKey = resolveBuiltinHandlerKey( typeHash, serializeContext );
                vector<uint8>       listPayload;

                auto writePayload = [&]( const void* pPtr )
                {
                    const SerializeContext::BinaryWriteFn* pWriter = serializeContext.findBinaryWriter( handlerKey );
                    if ( pWriter != nullptr )
                    {
                        ( *pWriter )( pPtr, listPayload );
                        return true;
                    }
                    const TypeInfo* pInfo = engine::getTypeRegistry().findType( typeHash );
                    if ( pInfo != nullptr )
                    {
                        BinarySerializer::serialize( pPtr, *pInfo, listPayload, serializeContext );
                        return true;
                    }
                    return false;
                };

                bool bOk{ false };
                bool bMatched{ false };

#define TRY_PACK( NameStr, CppType )                                                  \
    if ( bMatched == false && engine::getTypeRegistry().isType( typeHash, NameStr ) ) \
    {                                                                                 \
        bMatched                 = true;                                              \
        const CppType typedValue = value.getValue<CppType>();                         \
        bOk                      = writePayload( &typedValue );                       \
    }

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) TRY_PACK( #Canon, CppType )
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
#undef TRY_PACK

                if ( bMatched == false )
                {
                    SW_LOG_WARNING( "Unsupported arg type for pack: %#", typeName );
                    return false;
                }
                if ( bOk == false )
                    return false;

                const uint32 typeNameHash = typeHash.getHash();
                const uint32 size         = static_cast<uint32>( listPayload.size() );
                const uint8* pTh          = reinterpret_cast<const uint8*>( &typeNameHash );
                const uint8* pSz          = reinterpret_cast<const uint8*>( &size );
                outBytes.insert( outBytes.end(), pTh, pTh + sizeof( uint32 ) );
                outBytes.insert( outBytes.end(), pSz, pSz + sizeof( uint32 ) );
                outBytes.insert( outBytes.end(), listPayload.begin(), listPayload.end() );
                return true;
            }

            static bool unpackOneArg( TaskArgs& args, string_view typeName, const uint8* pData, size_t dataSize,
                                      size_t& offset, const SerializeContext& serializeContext )
            {
                if ( offset + sizeof( uint32 ) * 2 > dataSize )
                    return false;
                uint32 typeNameHash{ 0 };
                uint32 size{ 0 };
                Memory::copy( &typeNameHash, pData + offset, sizeof( uint32 ) );
                offset += sizeof( uint32 );
                Memory::copy( &size, pData + offset, sizeof( uint32 ) );
                offset += sizeof( uint32 );
                if ( offset + size > dataSize )
                    return false;

                const hashed_string typeHash( typeName.data(), static_cast<uint32>( typeName.size() ) );

                // **보낸 쪽이 적어 둔 타입과 받는 쪽이 기대하는 타입이 같아야 한다.** 시그니처가 어긋난 채로 주고받으면(빌드가 다르거나
                // 모듈이 핫 리로드된 뒤, 또는 봉투가 망가진 채로) 같은 바이트를 **다른 타입으로 읽어**
                // 터지지 않고 값만 조용히 달라진다. `float32 1.5f` 를 `int32` 로 읽으면
                // `1069547520` 이 되는 식이다. 별칭 때문에 표기가 다를 수 있으므로 등록부의
                // 정규 이름으로 비교하고, 등록부가 그 해시를 모르면 판단하지 않는다(그때는
                // 아래 타입 분기가 어차피 걸러 낸다).
                const hashed_string wireTypeName = engine::getTypeRegistry().canonicalTypeNameByHash( typeNameHash );
                if ( wireTypeName.empty() == false && engine::getTypeRegistry().isType( typeHash, wireTypeName ) == false )
                {
                    SW_LOG_WARNING( "RPC arg type mismatch: wire '%#', expected '%#'", wireTypeName.c_str(), typeName );
                    return false;
                }

                const hashed_string handlerKey = resolveBuiltinHandlerKey( typeHash, serializeContext );
                size_t              local{ 0 };
                bool                bMatched{ false };

                auto readInto = [&]( void* pDestination ) -> bool
                {
                    const SerializeContext::BinaryReadFn* pReader = serializeContext.findBinaryReader( handlerKey );
                    if ( pReader != nullptr )
                        return ( *pReader )( pDestination, pData + offset, size, local );
                    return false;
                };

#define TRY_UNPACK( NameStr, CppType )                                                \
    if ( bMatched == false && engine::getTypeRegistry().isType( typeHash, NameStr ) ) \
    {                                                                                 \
        bMatched = true;                                                              \
        CppType typedValue{};                                                         \
        if ( readInto( &typedValue ) == false )                                       \
            return false;                                                             \
        args.add( typedValue );                                                       \
    }

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) TRY_UNPACK( #Canon, CppType )
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
#undef TRY_UNPACK

                if ( bMatched == false )
                {
                    SW_LOG_WARNING( "Unsupported arg type for unpack: %#", typeName );
                    return false;
                }

                offset += size;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ReflectionRPC" );

    bool ReflectionRPC::packCall( RPCEnvelope& outEnvelope, const hashed_string& typeFqn, const hashed_string& methodName,
                                  const TaskArgs& args )
    {
        outEnvelope               = RPCEnvelope{};
        const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType( typeFqn );
        if ( pTypeInfo == nullptr )
            return false;
        const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
        if ( pFunc == nullptr )
            return false;
        if ( args.getCount() != pFunc->getParameterCount() )
        {
            SW_LOG_WARNING( "Arg count mismatch for %#::%#", typeFqn.c_str(), methodName.c_str() );
            return false;
        }

        outEnvelope._typeFqn     = typeFqn.c_str();
        outEnvelope._methodName  = methodName.c_str();
        outEnvelope._typeFqnHash = typeFqn.getHash();
        outEnvelope._methodHash  = methodName.getHash();
        outEnvelope._netRole     = static_cast<uint8>( pFunc->_metadata._netRole );
        outEnvelope._bReliable   = pFunc->_metadata._bReliable;

        const SerializeContext& serializeContext = SerializeContext::getDefault();
        const uint32            count            = args.getCount();
        const uint8*            pCountBytes      = reinterpret_cast<const uint8*>( &count );

        outEnvelope._argumentBytes.reserve( sizeof( uint32 ) + static_cast<size_t>( count ) * 32 );
        outEnvelope._argumentBytes.insert( outEnvelope._argumentBytes.end(), pCountBytes, pCountBytes + sizeof( uint32 ) );

        for ( uint32 argIndex = 0; argIndex < count; ++argIndex )
        {
            if ( ReflectionRPCInternal::packOneArg( outEnvelope._argumentBytes, pFunc->_listParameter[argIndex]._typeName, args.get( argIndex ), serializeContext ) == false )
                return false;
        }
        return true;
    }

    TaskValue ReflectionRPC::unpackAndInvoke( void* pInstance, const TypeInfo& instanceType, const RPCEnvelope& envelope )
    {
        if ( pInstance == nullptr || envelope._typeFqn.empty() || envelope._methodName.empty() )
            return {};

        // 봉투의 이름은 찾기만 한다(`findInterned`) — 받은 문자열로 전역 이름 표를 채우지 않는다.
        const hashed_string typeFqn    = hashed_string::findInterned( envelope._typeFqn );
        const hashed_string methodName = hashed_string::findInterned( envelope._methodName );
        const TypeInfo*     pTypeInfo  = typeFqn.empty() ? nullptr : engine::getTypeRegistry().findType( typeFqn );
        if ( pTypeInfo == nullptr || methodName.empty() )
            return {};
        // 인스턴스가 봉투의 타입(이나 그 자식)이어야 한다. 다른 타입을 적은 봉투는 그 메서드를 엉뚱한 객체 위에서 부른다.
        if ( &instanceType != pTypeInfo && instanceType.isDerivedFrom( pTypeInfo ) == false )
        {
            SW_LOG_WARNING( "RPC envelope names %# but the instance is %# — not invoked", typeFqn.c_str(), instanceType._fullyQualifiedName.c_str() );
            return {};
        }
        const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
        if ( pFunc == nullptr )
            return {};
        // RPC 로 표시된 메서드만 받는다(`FUNCTION( Server | Client | Multicast )`). 로컬 메서드를 봉투로 부르게 두면 모든 리플렉션 메서드가
        // 원격 호출 표면이 된다.
        if ( pFunc->_metadata._netRole == FunctionNetRole::Local )
        {
            SW_LOG_WARNING( "RPC envelope targets %#::%# which is not an RPC — not invoked", typeFqn.c_str(), methodName.c_str() );
            return {};
        }

        TaskArgs                unpacked;
        const SerializeContext& serializeContext = SerializeContext::getDefault();
        size_t                  offset{ 0 };
        if ( envelope._argumentBytes.size() < sizeof( uint32 ) )
            return {};
        uint32 count{ 0 };
        Memory::copy( &count, envelope._argumentBytes.data(), sizeof( uint32 ) );
        offset += sizeof( uint32 );
        if ( count != pFunc->getParameterCount() )
            return {};

        for ( uint32 argIndex = 0; argIndex < count; ++argIndex )
        {
            if ( ReflectionRPCInternal::unpackOneArg( unpacked, pFunc->_listParameter[argIndex]._typeName, envelope._argumentBytes.data(),
                                                      envelope._argumentBytes.size(), offset, serializeContext ) == false )
                return {};
        }

        return engine::getTypeRegistry().invokeMethod( pInstance, typeFqn, methodName, unpacked );
    }
} // namespace sw
