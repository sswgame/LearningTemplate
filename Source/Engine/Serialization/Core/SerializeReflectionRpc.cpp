/**
 * @file SerializeReflectionRpc.cpp
 * @brief RPC 인자 마샬링 — `ReflectionRpc` 의 구현.
 *
 * @details 이 파일이 **Serialization 에 있는 이유**: 내용 전부가 "인자를 바이트로 싣고 다시
 *          꺼내는" 일이고, 그 규약은 `SerializeContext` 의 핸들러 표와 `BinarySerializer` 가
 *          정한다. 선언(`Reflection/Rpc/ReflectionRpc.h`)은 리플렉션이 노출하는 API 로 남는다.
 *
 *          같은 규칙이 `SerializeReflectAny.cpp` 에도 적용된다 —
 *          **리플렉션 타입의 인코딩은 Serialization 이 갖는다.** 반대로 두면 Reflection 과
 *          Serialization 이 서로를 참조해 티어 순서를 정할 수 없게 된다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ObjectHandle.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Rpc/ReflectionRpc.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{
    namespace
    {
        struct ReflectionRpcInternal
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

                (void)typeNameHash;
                const hashed_string typeHash( typeName.data(), static_cast<uint32>( typeName.size() ) );
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
    SW_LOG_CALLER( "ReflectionRpc" );

    bool ReflectionRpc::packCall( RpcEnvelope& out, const hashed_string& typeFqn, const hashed_string& methodName,
                                  const TaskArgs& args )
    {
        out                       = RpcEnvelope{};
        const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType( typeFqn );
        if ( pTypeInfo == nullptr )
            return false;
        const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
        if ( pFunc == nullptr )
            return false;
        if ( args.getCount() != static_cast<uint32>( pFunc->_listParameterTypeName.size() ) )
        {
            SW_LOG_WARNING( "Arg count mismatch for %#::%#", typeFqn.c_str(), methodName.c_str() );
            return false;
        }

        out._typeFqn     = typeFqn.c_str();
        out._methodName  = methodName.c_str();
        out._typeFqnHash = typeFqn.getHash();
        out._methodHash  = methodName.getHash();
        out._netRole     = static_cast<uint8>( pFunc->_metadata._netRole );
        out._bReliable   = pFunc->_metadata._bReliable;

        const SerializeContext& serializeContext = SerializeContext::getDefault();
        const uint32            count            = args.getCount();
        const uint8*            pCountBytes      = reinterpret_cast<const uint8*>( &count );

        out._argumentBytes.reserve( sizeof( uint32 ) + static_cast<size_t>( count ) * 32 );
        out._argumentBytes.insert( out._argumentBytes.end(), pCountBytes, pCountBytes + sizeof( uint32 ) );

        for ( uint32 argIndex = 0; argIndex < count; ++argIndex )
        {
            if ( ReflectionRpcInternal::packOneArg( out._argumentBytes, pFunc->_listParameterTypeName[argIndex], args.get( argIndex ), serializeContext ) == false )
                return false;
        }
        return true;
    }

    TaskValue ReflectionRpc::unpackAndInvoke( void* pInstance, const RpcEnvelope& envelope )
    {
        if ( pInstance == nullptr || envelope._typeFqn.empty() || envelope._methodName.empty() )
            return {};

        const hashed_string typeFqn( envelope._typeFqn.c_str() );
        const hashed_string methodName( envelope._methodName.c_str() );
        const TypeInfo*     pTypeInfo = engine::getTypeRegistry().findType( typeFqn );
        if ( pTypeInfo == nullptr )
            return {};
        const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
        if ( pFunc == nullptr )
            return {};

        TaskArgs                unpacked;
        const SerializeContext& serializeContext = SerializeContext::getDefault();
        size_t                  offset{ 0 };
        if ( envelope._argumentBytes.size() < sizeof( uint32 ) )
            return {};
        uint32 count{ 0 };
        Memory::copy( &count, envelope._argumentBytes.data(), sizeof( uint32 ) );
        offset += sizeof( uint32 );
        if ( count != static_cast<uint32>( pFunc->_listParameterTypeName.size() ) )
            return {};

        for ( uint32 argIndex = 0; argIndex < count; ++argIndex )
        {
            if ( ReflectionRpcInternal::unpackOneArg( unpacked, pFunc->_listParameterTypeName[argIndex], envelope._argumentBytes.data(),
                                                      envelope._argumentBytes.size(), offset, serializeContext ) == false )
                return {};
        }

        return engine::getTypeRegistry().invokeMethod( pInstance, typeFqn, methodName, unpacked );
    }
} // namespace sw
