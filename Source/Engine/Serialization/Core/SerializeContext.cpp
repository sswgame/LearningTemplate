#include "pch.h"

#include "Engine/Serialization/Core/SerializeContext.h"

#include "Core/Container/ObjectHandle.h"

#include "Engine/ECS/ComponentHandle.h"
#include "Engine/ECS/Entity.h"
#include "Engine/Reflection/ReflectAny.h"

namespace sw
{

	void SerializeContext::registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn )
	{
		_mapBinaryWriters.insert_or_assign( typeName, std::move( writeFn ) );
		_mapBinaryReaders.insert_or_assign( typeName, std::move( readFn ) );
	}

	void SerializeContext::registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn )
	{
		_mapTextWriters.insert_or_assign( typeName, std::move( writeFn ) );
		_mapTextReaders.insert_or_assign( typeName, std::move( readFn ) );
	}

	const SerializeContext::BinaryWriteFn* SerializeContext::findBinaryWriter( hashed_string typeName ) const
	{
		auto it = _mapBinaryWriters.find( typeName );
		return it != _mapBinaryWriters.end() ? &it->second : nullptr;
	}

	const SerializeContext::BinaryReadFn* SerializeContext::findBinaryReader( hashed_string typeName ) const
	{
		auto it = _mapBinaryReaders.find( typeName );
		return it != _mapBinaryReaders.end() ? &it->second : nullptr;
	}

	const SerializeContext::TextWriteFn* SerializeContext::findTextWriter( hashed_string typeName ) const
	{
		auto it = _mapTextWriters.find( typeName );
		return it != _mapTextWriters.end() ? &it->second : nullptr;
	}

	const SerializeContext::TextReadFn* SerializeContext::findTextReader( hashed_string typeName ) const
	{
		auto it = _mapTextReaders.find( typeName );
		return it != _mapTextReaders.end() ? &it->second : nullptr;
	}

	namespace
	{
		template <typename T>
		static void regBuiltinBin( SerializeContext& ctx, const utf8* pName )
		{
			if ( StringUtil::strcmp( pName, "string" ) == 0 || StringUtil::strcmp( pName, "hashed_string" ) == 0 )
				return;
			auto writeFn = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const uint8* pB = reinterpret_cast<const uint8*>( pPtr );
				listBuf.insert( listBuf.end(), pB, pB + sizeof( T ) );
			};
			auto readFn = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( T ) > size )
					return false;
				Memory::copy( pPtr, pData + offset, sizeof( T ) );
				offset += sizeof( T );
				return true;
			};
			ctx.registerBinaryHandler( hashed_string( pName ), writeFn, readFn );
			const string qualified = string( "sw::" ) + pName;
			ctx.registerBinaryHandler( hashed_string( qualified.c_str() ), writeFn, readFn );
		}
	} // namespace

	const SerializeContext& SerializeContext::getDefault()
	{
		static SerializeContext s_defaultCtx = []()
		{
			SerializeContext ctx;

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) regBuiltinBin<CppType>( ctx, #Canon );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER

			BinaryWriteFn strWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const string& str		= *static_cast<const string*>( pPtr );
				uint32		  len		= static_cast<uint32>( str.size() );
				const uint8*  pLenBytes = reinterpret_cast<const uint8*>( &len );
				listBuf.insert( listBuf.end(), pLenBytes, pLenBytes + sizeof( uint32 ) );
				if ( len > 0 )
				{
					const uint8* pSBytes = reinterpret_cast<const uint8*>( str.data() );
					listBuf.insert( listBuf.end(), pSBytes, pSBytes + len );
				}
			};

			BinaryReadFn strReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint32 ) > size )
					return false;

				uint32 len{ 0 };
				Memory::copy( &len, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );

				if ( offset + len > size )
					return false;

				static_cast<string*>( pPtr )->assign( reinterpret_cast<const utf8*>( pData + offset ), len );
				offset += len;
				return true;
			};

			ctx.registerBinaryHandler( hashed_string( "string" ), strWriteBin, strReadBin );
			ctx.registerBinaryHandler( hashed_string( "sw::string" ), strWriteBin, strReadBin );

			BinaryWriteFn hashedStrWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const hashed_string& str	   = *static_cast<const hashed_string*>( pPtr );
				uint32				 len	   = StringUtil::strlen( str.c_str() );
				const uint8*		 pLenBytes = reinterpret_cast<const uint8*>( &len );
				listBuf.insert( listBuf.end(), pLenBytes, pLenBytes + sizeof( uint32 ) );
				if ( len > 0 )
				{
					const uint8* pSBytes = reinterpret_cast<const uint8*>( str.c_str() );
					listBuf.insert( listBuf.end(), pSBytes, pSBytes + len );
				}
			};

			BinaryReadFn hashedStrReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint32 ) > size )
					return false;

				uint32 len{ 0 };
				Memory::copy( &len, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );

				if ( offset + len > size )
					return false;

				*static_cast<hashed_string*>( pPtr ) = hashed_string( reinterpret_cast<const utf8*>( pData + offset ), static_cast<uint32>( len ) );
				offset += len;
				return true;
			};

			ctx.registerBinaryHandler( hashed_string( "hashed_string" ), hashedStrWriteBin, hashedStrReadBin );
			ctx.registerBinaryHandler( hashed_string( "sw::hashed_string" ), hashedStrWriteBin, hashedStrReadBin );

#define SW_BUILTIN_TEXT_none( Canon, CppType )
#define SW_BUILTIN_TEXT_stoi( Canon, CppType )                                                                    \
	{                                                                                                             \
		auto writeFn = []( const void* pPtr ) { return sw::to_string( *static_cast<const CppType*>( pPtr ) ); };  \
		auto readFn	 = []( void* pPtr, string_view strView )                                                      \
		{                                                                                                         \
			const fixed_string<constant::kMaxBuffer64> strStr{ strView };                                         \
			utf8*									   pEndPtr{ nullptr };                                        \
			int64									   val = StringUtil::strtoll( strStr.c_str(), &pEndPtr, 10 ); \
			if ( pEndPtr == strStr.c_str() && strStr.empty() == false )                                           \
				return false;                                                                                     \
			*static_cast<CppType*>( pPtr ) = static_cast<CppType>( val );                                         \
			return true;                                                                                          \
		};                                                                                                        \
		ctx.registerTextHandler( hashed_string( #Canon ), writeFn, readFn );                                      \
		const string qualified = string( "sw::" ) + #Canon;                                                       \
		ctx.registerTextHandler( hashed_string( qualified.c_str() ), writeFn, readFn );                           \
	}
#define SW_BUILTIN_TEXT_stoll( Canon, CppType )                                                                   \
	{                                                                                                             \
		auto writeFn = []( const void* pPtr ) { return sw::to_string( *static_cast<const CppType*>( pPtr ) ); };  \
		auto readFn	 = []( void* pPtr, string_view strView )                                                      \
		{                                                                                                         \
			const fixed_string<constant::kMaxBuffer64> strStr{ strView };                                         \
			utf8*									   pEndPtr{ nullptr };                                        \
			int64									   val = StringUtil::strtoll( strStr.c_str(), &pEndPtr, 10 ); \
			if ( pEndPtr == strStr.c_str() && strStr.empty() == false )                                           \
				return false;                                                                                     \
			*static_cast<CppType*>( pPtr ) = static_cast<CppType>( val );                                         \
			return true;                                                                                          \
		};                                                                                                        \
		ctx.registerTextHandler( hashed_string( #Canon ), writeFn, readFn );                                      \
		const string qualified = string( "sw::" ) + #Canon;                                                       \
		ctx.registerTextHandler( hashed_string( qualified.c_str() ), writeFn, readFn );                           \
	}
#define SW_BUILTIN_TEXT_stoul( Canon, CppType )                                                                    \
	{                                                                                                              \
		auto writeFn = []( const void* pPtr ) { return sw::to_string( *static_cast<const CppType*>( pPtr ) ); };   \
		auto readFn	 = []( void* pPtr, string_view strView )                                                       \
		{                                                                                                          \
			const fixed_string<constant::kMaxBuffer64> strStr{ strView };                                          \
			utf8*									   pEndPtr{ nullptr };                                         \
			uint64									   val = StringUtil::strtoull( strStr.c_str(), &pEndPtr, 10 ); \
			if ( pEndPtr == strStr.c_str() && strStr.empty() == false )                                            \
				return false;                                                                                      \
			*static_cast<CppType*>( pPtr ) = static_cast<CppType>( val );                                          \
			return true;                                                                                           \
		};                                                                                                         \
		ctx.registerTextHandler( hashed_string( #Canon ), writeFn, readFn );                                       \
		const string qualified = string( "sw::" ) + #Canon;                                                        \
		ctx.registerTextHandler( hashed_string( qualified.c_str() ), writeFn, readFn );                            \
	}
#define SW_BUILTIN_TEXT_stoull( Canon, CppType )                                                                   \
	{                                                                                                              \
		auto writeFn = []( const void* pPtr ) { return sw::to_string( *static_cast<const CppType*>( pPtr ) ); };   \
		auto readFn	 = []( void* pPtr, string_view strView )                                                       \
		{                                                                                                          \
			const fixed_string<constant::kMaxBuffer64> strStr{ strView };                                          \
			utf8*									   pEndPtr{ nullptr };                                         \
			uint64									   val = StringUtil::strtoull( strStr.c_str(), &pEndPtr, 10 ); \
			if ( pEndPtr == strStr.c_str() && strStr.empty() == false )                                            \
				return false;                                                                                      \
			*static_cast<CppType*>( pPtr ) = static_cast<CppType>( val );                                          \
			return true;                                                                                           \
		};                                                                                                         \
		ctx.registerTextHandler( hashed_string( #Canon ), writeFn, readFn );                                       \
		const string qualified = string( "sw::" ) + #Canon;                                                        \
		ctx.registerTextHandler( hashed_string( qualified.c_str() ), writeFn, readFn );                            \
	}
#define SW_BUILTIN_TEXT_stof( Canon, CppType )                                                                   \
	{                                                                                                            \
		auto writeFn = []( const void* pPtr ) { return sw::to_string( *static_cast<const CppType*>( pPtr ) ); }; \
		auto readFn	 = []( void* pPtr, string_view strView )                                                     \
		{                                                                                                        \
			const fixed_string<constant::kMaxBuffer64> strStr{ strView };                                        \
			utf8*									   pEndPtr{ nullptr };                                       \
			float32									   val = StringUtil::strtof( strStr.c_str(), &pEndPtr );     \
			if ( pEndPtr == strStr.c_str() && strStr.empty() == false )                                          \
				return false;                                                                                    \
			*static_cast<CppType*>( pPtr ) = static_cast<CppType>( val );                                        \
			return true;                                                                                         \
		};                                                                                                       \
		ctx.registerTextHandler( hashed_string( #Canon ), writeFn, readFn );                                     \
		const string qualified = string( "sw::" ) + #Canon;                                                      \
		ctx.registerTextHandler( hashed_string( qualified.c_str() ), writeFn, readFn );                          \
	}
#define SW_BUILTIN_TEXT_stod( Canon, CppType )                                                                   \
	{                                                                                                            \
		auto writeFn = []( const void* pPtr ) { return sw::to_string( *static_cast<const CppType*>( pPtr ) ); }; \
		auto readFn	 = []( void* pPtr, string_view strView )                                                     \
		{                                                                                                        \
			const fixed_string<constant::kMaxBuffer64> strStr{ strView };                                        \
			utf8*									   pEndPtr{ nullptr };                                       \
			float64									   val = StringUtil::strtod( strStr.c_str(), &pEndPtr );     \
			if ( pEndPtr == strStr.c_str() && strStr.empty() == false )                                          \
				return false;                                                                                    \
			*static_cast<CppType*>( pPtr ) = static_cast<CppType>( val );                                        \
			return true;                                                                                         \
		};                                                                                                       \
		ctx.registerTextHandler( hashed_string( #Canon ), writeFn, readFn );                                     \
		const string qualified = string( "sw::" ) + #Canon;                                                      \
		ctx.registerTextHandler( hashed_string( qualified.c_str() ), writeFn, readFn );                          \
	}

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) SW_BUILTIN_TEXT_##TextConv( Canon, CppType )
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
#undef SW_BUILTIN_TEXT_none
#undef SW_BUILTIN_TEXT_stoi
#undef SW_BUILTIN_TEXT_stoll
#undef SW_BUILTIN_TEXT_stoul
#undef SW_BUILTIN_TEXT_stoull
#undef SW_BUILTIN_TEXT_stof
#undef SW_BUILTIN_TEXT_stod

			auto boolWrite = []( const void* pPtr )
			{ return *static_cast<const bool*>( pPtr ) ? "true" : "false"; };
			auto boolRead = []( void* pPtr, string_view strView )
			{
				*static_cast<bool*>( pPtr ) = ( strView == "true" || strView == "1" );
				return true;
			};
			ctx.registerTextHandler( hashed_string( "bool" ), boolWrite, boolRead );
			ctx.registerTextHandler( hashed_string( "sw::bool" ), boolWrite, boolRead );

			TextWriteFn strWriteTxt = []( const void* pPtr )
			{ return string( static_cast<const string*>( pPtr )->c_str() ); };
			TextReadFn strReadTxt = []( void* pPtr, string_view strView )
			{
				string_view sv = strView;
				if ( sv.empty() == false && sv.front() == '"' )
					sv.remove_prefix( 1 );
				if ( sv.empty() == false && sv.back() == '"' )
					sv.remove_suffix( 1 );
				*static_cast<string*>( pPtr ) = string( sv );
				return true;
			};

			ctx.registerTextHandler( hashed_string( "string" ), strWriteTxt, strReadTxt );
			ctx.registerTextHandler( hashed_string( "sw::string" ), strWriteTxt, strReadTxt );

			TextWriteFn hashedStrWriteTxt = []( const void* pPtr )
			{ return string( static_cast<const hashed_string*>( pPtr )->c_str() ); };
			TextReadFn hashedStrReadTxt = []( void* pPtr, string_view strView )
			{
				string_view sv = strView;
				if ( sv.empty() == false && sv.front() == '"' )
					sv.remove_prefix( 1 );
				if ( sv.empty() == false && sv.back() == '"' )
					sv.remove_suffix( 1 );
				*static_cast<hashed_string*>( pPtr ) = hashed_string( sv.data(), static_cast<uint32>( sv.size() ) );
				return true;
			};

			ctx.registerTextHandler( hashed_string( "hashed_string" ), hashedStrWriteTxt, hashedStrReadTxt );
			ctx.registerTextHandler( hashed_string( "sw::hashed_string" ), hashedStrWriteTxt, hashedStrReadTxt );

			auto packedWrite = []( const void* pPtr ) -> string
			{
				const uint64 packed = static_cast<const ObjectHandle*>( pPtr )->packed();
				return sw::to_string( packed );
			};
			auto packedRead = []( void* pPtr, string_view strView ) -> bool
			{
				const fixed_string<constant::kMaxBuffer32> strStr{ strView };
				utf8*									   pEndPtr{ nullptr };
				uint64									   packed = StringUtil::strtoull( strStr.c_str(), &pEndPtr, 10 );
				if ( pEndPtr == strStr.c_str() && strStr.empty() == false )
					return false;
				*static_cast<ObjectHandle*>( pPtr ) = ObjectHandle::fromPacked( packed );
				return true;
			};
			ctx.registerTextHandler( hashed_string( "ObjectHandle" ), packedWrite, packedRead );
			ctx.registerTextHandler( hashed_string( "Entity" ), packedWrite, packedRead );

			ctx.registerTextHandler(
				hashed_string( "ComponentHandle" ),
				[]( const void* pPtr )
			{
				const auto&							  handle = *static_cast<const ComponentHandle*>( pPtr );
				StringBuilder<constant::kMaxBuffer64> sb;
				sb.append( handle.entity().packed() ).append( ':' ).append( handle.typeId() );
				return string{ sb.c_str(), sb.size() };
			},
				[]( void* pPtr, string_view strView )
			{
				const fixed_string<constant::kMaxBuffer64> strStr{ strView };
				const uint32							   sep = strStr.find( ':' );
				if ( sep == fixed_string<constant::kMaxBuffer64>::npos )
					return false;
				utf8*  pEndPtr{ nullptr };
				uint64 packed = StringUtil::strtoull( strStr.c_str(), &pEndPtr, 10 );
				uint32 typeId = static_cast<uint32>( StringUtil::strtoull( strStr.c_str() + sep + 1, &pEndPtr, 10 ) );
				*static_cast<ComponentHandle*>( pPtr ) =
					ComponentHandle::make( ObjectHandle::fromPacked( packed ), typeId );
				return true;
			} );

			registerReflectAnyHandlers( ctx );
			return ctx;
		}();

		return s_defaultCtx;
	}

} // namespace sw
