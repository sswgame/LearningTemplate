#include "pch.h"

#include "Engine/Reflection/Rpc/ReflectionRpc.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ObjectHandle.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionTypes.h"
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

			static bool packOneArg( vector<uint8>& listOut, string_view typeName, const TaskValue& value,
									const SerializeContext& serializeContext )
			{
				const hashed_string typeHash( typeName.data(), static_cast<uint32>( typeName.size() ) );
				const hashed_string handlerKey = resolveBuiltinHandlerKey( typeHash, serializeContext );
				vector<uint8>		listPayload;

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

				bool ok{ false };
				bool matched{ false };

#define TRY_PACK( NameStr, CppType )                                                 \
	if ( matched == false && engine::getTypeRegistry().isType( typeHash, NameStr ) ) \
	{                                                                                \
		matched			= true;                                                      \
		const CppType v = value.getValue<CppType>();                                 \
		ok				= writePayload( &v );                                        \
	}

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) TRY_PACK( #Canon, CppType )
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
#undef TRY_PACK

				if ( matched == false )
				{
					SW_LOG_WARNING( "Unsupported arg type for pack: %#", typeName );
					return false;
				}
				if ( ok == false )
					return false;

				const uint32 typeNameHash = typeHash.getHash();
				const uint32 size		  = static_cast<uint32>( listPayload.size() );
				const uint8* pTh		  = reinterpret_cast<const uint8*>( &typeNameHash );
				const uint8* pSz		  = reinterpret_cast<const uint8*>( &size );
				listOut.insert( listOut.end(), pTh, pTh + sizeof( uint32 ) );
				listOut.insert( listOut.end(), pSz, pSz + sizeof( uint32 ) );
				listOut.insert( listOut.end(), listPayload.begin(), listPayload.end() );
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
				size_t				local{ 0 };
				bool				matched{ false };

				auto readInto = [&]( void* pDestination ) -> bool
				{
					const SerializeContext::BinaryReadFn* pReader = serializeContext.findBinaryReader( handlerKey );
					if ( pReader != nullptr )
						return ( *pReader )( pDestination, pData + offset, size, local );
					return false;
				};

#define TRY_UNPACK( NameStr, CppType )                                               \
	if ( matched == false && engine::getTypeRegistry().isType( typeHash, NameStr ) ) \
	{                                                                                \
		matched = true;                                                              \
		CppType v{};                                                                 \
		if ( readInto( &v ) == false )                                               \
			return false;                                                            \
		args.add( v );                                                               \
	}

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) TRY_UNPACK( #Canon, CppType )
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
#undef TRY_UNPACK

				if ( matched == false )
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
		out						  = RpcEnvelope{};
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

		out._typeFqn	 = typeFqn.c_str();
		out._methodName	 = methodName.c_str();
		out._typeFqnHash = typeFqn.getHash();
		out._methodHash	 = methodName.getHash();
		out._netRole	 = static_cast<uint8>( pFunc->_metadata._netRole );
		out._bReliable	 = pFunc->_metadata._bReliable;

		const SerializeContext& serializeContext = SerializeContext::getDefault();
		const uint32			count			 = args.getCount();
		const uint8*			pCountBytes		 = reinterpret_cast<const uint8*>( &count );

		out._listArgumentBytes.reserve( sizeof( uint32 ) + count * 32 );
		out._listArgumentBytes.insert( out._listArgumentBytes.end(), pCountBytes, pCountBytes + sizeof( uint32 ) );

		for ( uint32 argIndex = 0; argIndex < count; ++argIndex )
		{
			if ( ReflectionRpcInternal::packOneArg( out._listArgumentBytes, pFunc->_listParameterTypeName[argIndex], args.get( argIndex ), serializeContext ) == false )
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
		const TypeInfo*		pTypeInfo = engine::getTypeRegistry().findType( typeFqn );
		if ( pTypeInfo == nullptr )
			return {};
		const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
		if ( pFunc == nullptr )
			return {};

		TaskArgs				unpacked;
		const SerializeContext& serializeContext = SerializeContext::getDefault();
		size_t					offset{ 0 };
		if ( envelope._listArgumentBytes.size() < sizeof( uint32 ) )
			return {};
		uint32 count{ 0 };
		Memory::copy( &count, envelope._listArgumentBytes.data(), sizeof( uint32 ) );
		offset += sizeof( uint32 );
		if ( count != static_cast<uint32>( pFunc->_listParameterTypeName.size() ) )
			return {};

		for ( uint32 argIndex = 0; argIndex < count; ++argIndex )
		{
			if ( ReflectionRpcInternal::unpackOneArg( unpacked, pFunc->_listParameterTypeName[argIndex], envelope._listArgumentBytes.data(),
													  envelope._listArgumentBytes.size(), offset, serializeContext ) == false )
				return {};
		}

		return engine::getTypeRegistry().invokeMethod( pInstance, typeFqn, methodName, unpacked );
	}

	bool ReflectionRpc::packAndInvoke( void* pInstance, const hashed_string& typeFqn, const hashed_string& methodName,
									   const TaskArgs& args, TaskValue* pOutResult )
	{
		RpcEnvelope envelope;
		if ( packCall( envelope, typeFqn, methodName, args ) == false )
			return false;
		TaskValue result = unpackAndInvoke( pInstance, envelope );
		if ( pOutResult != nullptr )
			*pOutResult = result;
		return true;
	}
} // namespace sw
