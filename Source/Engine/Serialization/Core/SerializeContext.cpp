#include "pch.h"

#include "Engine/Serialization/Core/SerializeContext.h"

#include "Engine/Object/Component/ComponentHandle.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectAny.h"

#include "Core/Concurrency/Atomic.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"

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
			if ( StringUtil::strcmp( pName, "string" ) == 0 || StringUtil::strcmp( pName, "hashed_string" ) == 0 ||
				 StringUtil::strcmp( pName, "AtomicBool" ) == 0 || StringUtil::strcmp( pName, "TagID" ) == 0 )
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
		}

		template <int32 kCount>
		string formatFloatComma( const float32* pVals )
		{
			StringBuilder<constant::kMaxBuffer64> sb;
			for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
			{
				if ( axisIndex > 0 )
					sb.append( ',' );
				sb.append( pVals[axisIndex] );
			}
			return string{ sb.c_str(), sb.size() };
		}

		template <int32 kCount>
		bool parseFloatComma( string_view text, float32* pOut )
		{
			size_t start{ 0 };
			for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
			{
				const size_t	sep	  = text.find( ',', start );
				const string_view token = text.substr( start, sep == string_view::npos ? string_view::npos : sep - start );
				if ( token.empty() )
					return false;
				const fixed_string<constant::kMaxBuffer64> tokenNt{ token };
				utf8*									   pEndPtr{ nullptr };
				pOut[axisIndex] = StringUtil::strtof( tokenNt.c_str(), &pEndPtr );
				if ( pEndPtr == tokenNt.c_str() && tokenNt.empty() == false )
					return false;
				if ( sep == string_view::npos )
				{
					if ( axisIndex != kCount - 1 )
						return false;
					break;
				}
				start = sep + 1;
			}
			return true;
		}

		void registerFloatVectorTextHandlers( SerializeContext& ctx )
		{
			ctx.registerTextHandler(
				hashed_string( PredefinedNameType::NameType_float2 ),
				[]( const void* pPtr )
			{
				const float2&  value	  = *static_cast<const float2*>( pPtr );
				const float32  arrVals[2] = { value._x, value._y };
				return formatFloatComma<2>( arrVals );
			},
				[]( void* pPtr, string_view strView )
			{
				float32 arrVals[2]{};
				if ( parseFloatComma<2>( strView, arrVals ) == false )
					return false;
				*static_cast<float2*>( pPtr ) = float2( arrVals[0], arrVals[1] );
				return true;
			} );

			ctx.registerTextHandler(
				hashed_string( PredefinedNameType::NameType_float3 ),
				[]( const void* pPtr )
			{
				const float3&  value	  = *static_cast<const float3*>( pPtr );
				const float32  arrVals[3] = { value._x, value._y, value._z };
				return formatFloatComma<3>( arrVals );
			},
				[]( void* pPtr, string_view strView )
			{
				float32 arrVals[3]{};
				if ( parseFloatComma<3>( strView, arrVals ) == false )
					return false;
				*static_cast<float3*>( pPtr ) = float3( arrVals[0], arrVals[1], arrVals[2] );
				return true;
			} );

			ctx.registerTextHandler(
				hashed_string( PredefinedNameType::NameType_float4 ),
				[]( const void* pPtr )
			{
				const float4&  value	  = *static_cast<const float4*>( pPtr );
				const float32  arrVals[4] = { value._x, value._y, value._z, value._w };
				return formatFloatComma<4>( arrVals );
			},
				[]( void* pPtr, string_view strView )
			{
				float32 arrVals[4]{};
				if ( parseFloatComma<4>( strView, arrVals ) == false )
					return false;
				*static_cast<float4*>( pPtr ) = float4( arrVals[0], arrVals[1], arrVals[2], arrVals[3] );
				return true;
			} );
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

			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_string ), strWriteBin, strReadBin );

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

			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), hashedStrWriteBin, hashedStrReadBin );

			BinaryWriteFn atomicBoolWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const uint8 flag = static_cast<const AtomicBool*>( pPtr )->load() ? 1 : 0;
				listBuf.push_back( flag );
			};
			BinaryReadFn atomicBoolReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint8 ) > size )
					return false;
				static_cast<AtomicBool*>( pPtr )->store( pData[offset] != 0 );
				offset += sizeof( uint8 );
				return true;
			};
			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_AtomicBool ), atomicBoolWriteBin, atomicBoolReadBin );

			BinaryWriteFn tagIdWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const TagID& tag = *static_cast<const TagID*>( pPtr );
				const utf8*	 pStr = tag._pString != nullptr ? tag._pString : "";
				const uint32 len  = static_cast<uint32>( StringUtil::strlen( pStr ) );
				const uint8* pLen = reinterpret_cast<const uint8*>( &len );
				listBuf.insert( listBuf.end(), pLen, pLen + sizeof( uint32 ) );
				if ( len > 0 )
				{
					const uint8* pBytes = reinterpret_cast<const uint8*>( pStr );
					listBuf.insert( listBuf.end(), pBytes, pBytes + len );
				}
			};
			BinaryReadFn tagIdReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint32 ) > size )
					return false;
				uint32 len{ 0 };
				Memory::copy( &len, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );
				if ( offset + len > size )
					return false;
				if ( len == 0 )
				{
					*static_cast<TagID*>( pPtr ) = TagID{};
					return true;
				}
				const string_view text( reinterpret_cast<const utf8*>( pData + offset ), len );
				offset += len;
				*static_cast<TagID*>( pPtr ) = requestTag( text );
				return true;
			};
			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_TagID ), tagIdWriteBin, tagIdReadBin );

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
		ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_##Canon ), writeFn, readFn );        \
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
		ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_##Canon ), writeFn, readFn );        \
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
		ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_##Canon ), writeFn, readFn );         \
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
		ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_##Canon ), writeFn, readFn );         \
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
		ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_##Canon ), writeFn, readFn );       \
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
		ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_##Canon ), writeFn, readFn );       \
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
			ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_bool ), boolWrite, boolRead );

			auto atomicBoolWrite = []( const void* pPtr )
			{ return static_cast<const AtomicBool*>( pPtr )->load() ? "true" : "false"; };
			auto atomicBoolRead = []( void* pPtr, string_view strView )
			{
				static_cast<AtomicBool*>( pPtr )->store( strView == "true" || strView == "1" );
				return true;
			};
			ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_AtomicBool ), atomicBoolWrite, atomicBoolRead );

			auto tagIdWrite = []( const void* pPtr ) -> string
			{
				const TagID& tag = *static_cast<const TagID*>( pPtr );
				if ( tag._pString != nullptr )
					return string( tag._pString );
				return {};
			};
			auto tagIdRead = []( void* pPtr, string_view strView ) -> bool
			{
				string_view text = strView;
				if ( text.size() >= 4 && text.substr( 0, 4 ) == "str:" )
					text.remove_prefix( 4 );
				if ( text.empty() )
				{
					*static_cast<TagID*>( pPtr ) = TagID{};
					return true;
				}
				*static_cast<TagID*>( pPtr ) = requestTag( text );
				return true;
			};
			ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_TagID ), tagIdWrite, tagIdRead );

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

			ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_string ), strWriteTxt, strReadTxt );

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

			ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), hashedStrWriteTxt, hashedStrReadTxt );

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

			ctx.registerTextHandler(
				hashed_string( "ComponentHandle" ),
				[]( const void* pPtr )
			{
				const auto&							  handle = *static_cast<const ComponentHandle*>( pPtr );
				StringBuilder<constant::kMaxBuffer64> sb;
				sb.append( handle.objectId() ).append( ':' ).append( handle.componentId() );
				return string{ sb.c_str(), sb.size() };
			},
				[]( void* pPtr, string_view strView )
			{
				const fixed_string<constant::kMaxBuffer64> strStr{ strView };
				const uint32							   sep = strStr.find( ':' );
				if ( sep == fixed_string<constant::kMaxBuffer64>::npos )
					return false;
				utf8*  pEndPtr{ nullptr };
				uint64 objectId	   = StringUtil::strtoull( strStr.c_str(), &pEndPtr, 10 );
				uint64 componentId = StringUtil::strtoull( strStr.c_str() + sep + 1, &pEndPtr, 10 );
				*static_cast<ComponentHandle*>( pPtr ) =
					ComponentHandle::makeOwned( objectId, componentId );
				return true;
			} );

			registerFloatVectorTextHandlers( ctx );
			registerReflectAnyHandlers( ctx );
			return ctx;
		}();

		return s_defaultCtx;
	}

} // namespace sw
