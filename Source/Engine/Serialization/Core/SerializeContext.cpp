#include "pch.h"

#include "Engine/Serialization/Core/SerializeContext.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/ComponentHandle.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Serialization/Core/BinaryStream.h"

namespace sw
{
	namespace
	{
		struct SerializeContextInternal
		{
			template <typename T>
			static void regBuiltinBin( SerializeContext& ctx, const utf8* pName )
			{
				if constexpr ( std::is_same_v<T, string> || std::is_same_v<T, hashed_string> ||
							   std::is_same_v<T, atomic<bool>> || std::is_same_v<T, TagID> )
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

			template <typename T>
			static void registerNumericTextHandler( SerializeContext& ctx, const hashed_string& typeName )
			{
				ctx.registerTextHandler(
					typeName,
					[]( const void* pPtr )
				{ return sw::to_string( *static_cast<const T*>( pPtr ) ); },
					[]( void* pPtr, string_view strView ) -> bool
				{
					const fixed_string<constant::kMaxBuffer64> tokenNt{ strView };
					utf8*									   pEndPtr{ nullptr };
					if constexpr ( std::is_floating_point_v<T> )
					{
						if constexpr ( sizeof( T ) == sizeof( float32 ) )
							*static_cast<T*>( pPtr ) = static_cast<T>( StringUtil::strtof( tokenNt.c_str(), &pEndPtr ) );
						else
							*static_cast<T*>( pPtr ) = static_cast<T>( StringUtil::strtod( tokenNt.c_str(), &pEndPtr ) );
					}
					else if constexpr ( std::is_unsigned_v<T> )
					{
						*static_cast<T*>( pPtr ) = static_cast<T>( StringUtil::strtoull( tokenNt.c_str(), &pEndPtr, 10 ) );
					}
					else
					{
						*static_cast<T*>( pPtr ) = static_cast<T>( StringUtil::strtoll( tokenNt.c_str(), &pEndPtr, 10 ) );
					}
					if ( pEndPtr == tokenNt.c_str() && tokenNt.empty() == false )
						return false;
					return true;
				} );
			}

			template <typename TVec, typename TElem, int32 kCount>
			static void registerVectorTextHandler( SerializeContext& ctx, const hashed_string& typeName )
			{
				ctx.registerTextHandler(
					typeName,
					[]( const void* pPtr )
				{
					const auto*							  pElem = reinterpret_cast<const TElem*>( pPtr );
					StringBuilder<constant::kMaxBuffer64> sb;
					for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
					{
						if ( axisIndex > 0 )
							sb.append( ',' );
						sb.append( pElem[axisIndex] );
					}
					return string{ sb.c_str(), sb.size() };
				},
					[]( void* pPtr, string_view strView )
				{
					auto*  pOut = reinterpret_cast<TElem*>( pPtr );
					size_t start{ 0 };
					for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
					{
						const size_t	  sep	= strView.find( ',', start );
						const string_view token = strView.substr( start, sep == string_view::npos ? string_view::npos : sep - start );
						if ( token.empty() )
							return false;
						const fixed_string<constant::kMaxBuffer64> tokenNt{ token };
						utf8*									   pEndPtr{ nullptr };
						if constexpr ( std::is_floating_point_v<TElem> )
							pOut[axisIndex] = static_cast<TElem>( StringUtil::strtod( tokenNt.c_str(), &pEndPtr ) );
						else
							pOut[axisIndex] = static_cast<TElem>( StringUtil::strtoll( tokenNt.c_str(), &pEndPtr, 10 ) );
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
				} );
			}

			static void registerFloatVectorTextHandlers( SerializeContext& ctx )
			{
				registerVectorTextHandler<float2, float32, 2>( ctx, hashed_string( PredefinedNameType::NameType_float2 ) );
				registerVectorTextHandler<float3, float32, 3>( ctx, hashed_string( PredefinedNameType::NameType_float3 ) );
				registerVectorTextHandler<float4, float32, 4>( ctx, hashed_string( PredefinedNameType::NameType_float4 ) );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	void SerializeContext::registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn )
	{
		_mapBinaryWriter.insert_or_assign( typeName, std::move( writeFn ) );
		_mapBinaryReader.insert_or_assign( typeName, std::move( readFn ) );
	}

	void SerializeContext::registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn )
	{
		_mapTextWriter.insert_or_assign( typeName, std::move( writeFn ) );
		_mapTextReader.insert_or_assign( typeName, std::move( readFn ) );
	}

	const SerializeContext::BinaryWriteFn* SerializeContext::findBinaryWriter( hashed_string typeName ) const
	{
		auto it = _mapBinaryWriter.find( typeName );
		return it != _mapBinaryWriter.end() ? &it->second : nullptr;
	}

	const SerializeContext::BinaryReadFn* SerializeContext::findBinaryReader( hashed_string typeName ) const
	{
		auto it = _mapBinaryReader.find( typeName );
		return it != _mapBinaryReader.end() ? &it->second : nullptr;
	}

	const SerializeContext::TextWriteFn* SerializeContext::findTextWriter( hashed_string typeName ) const
	{
		auto it = _mapTextWriter.find( typeName );
		return it != _mapTextWriter.end() ? &it->second : nullptr;
	}

	const SerializeContext::TextReadFn* SerializeContext::findTextReader( hashed_string typeName ) const
	{
		auto it = _mapTextReader.find( typeName );
		return it != _mapTextReader.end() ? &it->second : nullptr;
	}

	const SerializeContext& SerializeContext::getDefault()
	{
		static SerializeContext s_defaultCtx = []()
		{
			SerializeContext ctx;

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) SerializeContextInternal::regBuiltinBin<CppType>( ctx, #Canon );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER

			BinaryWriteFn strWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				BinaryStreamWriter writer{ listBuf };
				writer.writeString( *static_cast<const string*>( pPtr ) );
			};

			BinaryReadFn strReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				BinaryStreamReader reader{ pData, size };
				if ( reader.skip( offset ) == false )
					return false;
				if ( reader.readString( *static_cast<string*>( pPtr ) ) == false )
					return false;
				offset = reader.getOffset();
				return true;
			};

			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_string ), strWriteBin, strReadBin );

			BinaryWriteFn hashedStrWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				BinaryStreamWriter writer{ listBuf };
				writer.writeString( static_cast<const hashed_string*>( pPtr )->c_str() );
			};

			BinaryReadFn hashedStrReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				BinaryStreamReader reader{ pData, size };
				if ( reader.skip( offset ) == false )
					return false;
				string tempStr;
				if ( reader.readString( tempStr ) == false )
					return false;
				*static_cast<hashed_string*>( pPtr ) = hashed_string( tempStr.c_str() );
				offset								 = reader.getOffset();
				return true;
			};

			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), hashedStrWriteBin, hashedStrReadBin );

			BinaryWriteFn atomicBoolWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const uint8 flag = static_cast<const atomic<bool>*>( pPtr )->load() ? 1 : 0;
				listBuf.push_back( flag );
			};
			BinaryReadFn atomicBoolReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint8 ) > size )
					return false;
				static_cast<atomic<bool>*>( pPtr )->store( pData[offset] != 0 );
				offset += sizeof( uint8 );
				return true;
			};
			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_atomic_bool ), atomicBoolWriteBin, atomicBoolReadBin );

			BinaryWriteFn tagIdWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
			{
				const TagID&	   tag	= *static_cast<const TagID*>( pPtr );
				const utf8*		   pStr = tag._pString != nullptr ? tag._pString : "";
				BinaryStreamWriter writer{ listBuf };
				writer.writeString( pStr );
			};
			BinaryReadFn tagIdReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
			{
				BinaryStreamReader reader{ pData, size };
				if ( reader.skip( offset ) == false )
					return false;
				string tempStr;
				if ( reader.readString( tempStr ) == false )
					return false;
				if ( tempStr.empty() )
					*static_cast<TagID*>( pPtr ) = TagID{};
				else
					*static_cast<TagID*>( pPtr ) = TagID::request( tempStr );
				offset = reader.getOffset();
				return true;
			};
			ctx.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_TagID ), tagIdWriteBin, tagIdReadBin );

#define SW_BUILTIN_TEXT_none( Canon, CppType )
#define SW_BUILTIN_TEXT_stoi( Canon, CppType )	 SerializeContextInternal::registerNumericTextHandler<CppType>( ctx, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoll( Canon, CppType )	 SerializeContextInternal::registerNumericTextHandler<CppType>( ctx, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoul( Canon, CppType )	 SerializeContextInternal::registerNumericTextHandler<CppType>( ctx, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoull( Canon, CppType ) SerializeContextInternal::registerNumericTextHandler<CppType>( ctx, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stof( Canon, CppType )	 SerializeContextInternal::registerNumericTextHandler<CppType>( ctx, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stod( Canon, CppType )	 SerializeContextInternal::registerNumericTextHandler<CppType>( ctx, hashed_string( PredefinedNameType::NameType_##Canon ) );

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
			{ return static_cast<const atomic<bool>*>( pPtr )->load() ? "true" : "false"; };
			auto atomicBoolRead = []( void* pPtr, string_view strView )
			{
				static_cast<atomic<bool>*>( pPtr )->store( strView == "true" || strView == "1" );
				return true;
			};
			ctx.registerTextHandler( hashed_string( PredefinedNameType::NameType_atomic_bool ), atomicBoolWrite, atomicBoolRead );

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
				*static_cast<TagID*>( pPtr ) = TagID::request( text );
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

			SerializeContextInternal::registerFloatVectorTextHandlers( ctx );
			registerReflectAnyHandlers( ctx );
			return ctx;
		}();

		return s_defaultCtx;
	}

} // namespace sw
