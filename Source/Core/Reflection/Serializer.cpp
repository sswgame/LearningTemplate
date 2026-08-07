/**
 * @file Serializer.cpp
 * @brief Binary/JSON/XML 직렬화 구현
 */
#include "pch.h"

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Reflection/Serializer.h"
#include "Core/Reflection/ReflectionCore.h"

#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/String/string_splitter.h"
#include "Core/Utility/String/formatString.h"
#include "Core/Utility/String/StringBuilder.h"

#include <rapidxml.hpp>

namespace sw
{

	void SerializeContext::registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn )
	{
		_binaryWriters.insert_or_assign( typeName, std::move( writeFn ) );
		_binaryReaders.insert_or_assign( typeName, std::move( readFn ) );
	}

	void SerializeContext::registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn )
	{
		_textWriters.insert_or_assign( typeName, std::move( writeFn ) );
		_textReaders.insert_or_assign( typeName, std::move( readFn ) );
	}

	const SerializeContext::BinaryWriteFn* SerializeContext::findBinaryWriter( hashed_string typeName ) const
	{
		auto it = _binaryWriters.find( typeName );
		return it != _binaryWriters.end() ? &it->second : nullptr;
	}

	const SerializeContext::BinaryReadFn* SerializeContext::findBinaryReader( hashed_string typeName ) const
	{
		auto it = _binaryReaders.find( typeName );
		return it != _binaryReaders.end() ? &it->second : nullptr;
	}

	const SerializeContext::TextWriteFn* SerializeContext::findTextWriter( hashed_string typeName ) const
	{
		auto it = _textWriters.find( typeName );
		return it != _textWriters.end() ? &it->second : nullptr;
	}

	const SerializeContext::TextReadFn* SerializeContext::findTextReader( hashed_string typeName ) const
	{
		auto it = _textReaders.find( typeName );
		return it != _textReaders.end() ? &it->second : nullptr;
	}

	const SerializeContext& SerializeContext::getDefault()
	{
		static SerializeContext s_defaultCtx = []()
		{
			SerializeContext ctx;

			auto regPrimBin = [&ctx]( const utf8* name, auto sample )
			{
				using T = decltype( sample );
				hashed_string key( name );
				ctx.registerBinaryHandler(
					key,
					[]( const void* ptr, std::vector<uint8>& buf )
				{
					const uint8* b = reinterpret_cast<const uint8*>( ptr );
					buf.insert( buf.end(), b, b + sizeof( T ) );
				},
					[]( void* ptr, const uint8* data, size_t size, size_t& offset ) -> bool
				{
					if ( offset + sizeof( T ) > size )
						return false;
					std::memcpy( ptr, data + offset, sizeof( T ) );
					offset += sizeof( T );
					return true;
				} );
			};

#define REGISTER_TYPE( NameStr, CppType, ... ) regPrimBin( NameStr, CppType( 0 ) );
#define REGISTER_MATH_TYPE( NameStr, CppType ) regPrimBin( NameStr, CppType{} );
#include "Core/Utility/Predefined/PredefinedTypes.xxx"
#undef REGISTER_TYPE
#undef REGISTER_MATH_TYPE
			regPrimBin( "bool", bool( false ) );

			auto strWriteBin = []( const void* ptr, std::vector<uint8>& buf )
			{
				const auto&	 str	  = *static_cast<const std::string*>( ptr );
				uint32		 len	  = static_cast<uint32>( str.size() );
				const uint8* lenBytes = reinterpret_cast<const uint8*>( &len );
				buf.insert( buf.end(), lenBytes, lenBytes + sizeof( uint32 ) );
				if ( len > 0 )
				{
					const uint8* sBytes = reinterpret_cast<const uint8*>( str.data() );
					buf.insert( buf.end(), sBytes, sBytes + len );
				}
			};

			auto strReadBin = []( void* ptr, const uint8* data, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint32 ) > size )
					return false;

				uint32 len = 0;
				std::memcpy( &len, data + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );

				if ( offset + len > size )
					return false;

				static_cast<std::string*>( ptr )->assign( reinterpret_cast<const utf8*>( data + offset ), len );
				offset += len;
				return true;
			};

			ctx.registerBinaryHandler( hashed_string( "std::string" ), strWriteBin, strReadBin );
			ctx.registerBinaryHandler( hashed_string( "string" ), strWriteBin, strReadBin );

			auto hashedStrWriteBin = []( const void* ptr, std::vector<uint8>& buf )
			{
				const auto&	 str	  = *static_cast<const hashed_string*>( ptr );
				uint32		 len	  = static_cast<uint32>( StringUtil::strlen( str.c_str() ) );
				const uint8* lenBytes = reinterpret_cast<const uint8*>( &len );
				buf.insert( buf.end(), lenBytes, lenBytes + sizeof( uint32 ) );
				if ( len > 0 )
				{
					const uint8* sBytes = reinterpret_cast<const uint8*>( str.c_str() );
					buf.insert( buf.end(), sBytes, sBytes + len );
				}
			};

			auto hashedStrReadBin = []( void* ptr, const uint8* data, size_t size, size_t& offset ) -> bool
			{
				if ( offset + sizeof( uint32 ) > size )
					return false;

				uint32 len = 0;
				std::memcpy( &len, data + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );

				if ( offset + len > size )
					return false;

				std::string temp( reinterpret_cast<const utf8*>( data + offset ), len );
				*static_cast<hashed_string*>( ptr ) = hashed_string( temp.c_str() );
				offset += len;
				return true;
			};

			ctx.registerBinaryHandler( hashed_string( "sw::hashed_string" ), hashedStrWriteBin, hashedStrReadBin );
			ctx.registerBinaryHandler( hashed_string( "hashed_string" ), hashedStrWriteBin, hashedStrReadBin );

			auto toStr = []( auto val ) -> std::string
			{
				utf8 buf[64]{};
				formatstring( buf, static_cast<uint32>( sizeof( buf ) ), "%#", val );
				return std::string( buf );
			};

#define REGISTER_TYPE( NameStr, CppType, ConverterFunc )                                                          \
	ctx.registerTextHandler( hashed_string( NameStr ),                                                            \
							 [toStr]( const void* ptr ) { return toStr( *static_cast<const CppType*>( ptr ) ); }, \
							 []( void* ptr, std::string_view s ) { *static_cast<CppType*>( ptr ) = static_cast<CppType>( ConverterFunc( std::string( s ) ) ); return true; } );
#include "Core/Utility/Predefined/PredefinedTypes.xxx"
#undef REGISTER_TYPE

			ctx.registerTextHandler( hashed_string( "bool" ),
									 []( const void* ptr )
			{ return *static_cast<const bool*>( ptr ) ? "true" : "false"; },
									 []( void* ptr, std::string_view s )
			{
				*static_cast<bool*>( ptr ) = ( s == "true" || s == "1" );
				return true;
			} );

			auto strWriteTxt = []( const void* ptr )
			{
				return *static_cast<const std::string*>( ptr );
			};
			auto strReadTxt = []( void* ptr, std::string_view s )
			{
				std::string_view sv = s;
				if ( sv.empty() == false && sv.front() == '"' )
					sv.remove_prefix( 1 );
				if ( sv.empty() == false && sv.back() == '"' )
					sv.remove_suffix( 1 );
				*static_cast<std::string*>( ptr ) = std::string( sv );
				return true;
			};

			ctx.registerTextHandler( hashed_string( "std::string" ), strWriteTxt, strReadTxt );
			ctx.registerTextHandler( hashed_string( "string" ), strWriteTxt, strReadTxt );

			auto hashedStrWriteTxt = []( const void* ptr )
			{
				return std::string( static_cast<const hashed_string*>( ptr )->c_str() );
			};
			auto hashedStrReadTxt = []( void* ptr, std::string_view s )
			{
				std::string_view sv = s;
				if ( sv.empty() == false && sv.front() == '"' )
					sv.remove_prefix( 1 );
				if ( sv.empty() == false && sv.back() == '"' )
					sv.remove_suffix( 1 );
				std::string temp( sv );
				*static_cast<hashed_string*>( ptr ) = hashed_string( temp.c_str() );
				return true;
			};

			ctx.registerTextHandler( hashed_string( "sw::hashed_string" ), hashedStrWriteTxt, hashedStrReadTxt );
			ctx.registerTextHandler( hashed_string( "hashed_string" ), hashedStrWriteTxt, hashedStrReadTxt );

			return ctx;
		}();

		return s_defaultCtx;
	}

	static void serializeValueBinary( const void* valuePtr, const hashed_string& typeName,
									  std::vector<uint8>& buffer, const SerializeContext& ctx )
	{
		if ( const auto* writer = ctx.findBinaryWriter( typeName ) )
		{
			( *writer )( valuePtr, buffer );
			return;
		}

		if ( const EnumInfo* enumInfo = sw::core::getTypeRegistry().findEnum( typeName ) )
		{
			(void)enumInfo;
			int64		 val = *static_cast<const int64*>( valuePtr );
			const uint8* b	 = reinterpret_cast<const uint8*>( &val );
			buffer.insert( buffer.end(), b, b + sizeof( int64 ) );
			return;
		}

		if ( const TypeInfo* structInfo = sw::core::getTypeRegistry().findType( typeName ) )
		{
			BinarySerializer::serialize( valuePtr, *structInfo, buffer, ctx );
			return;
		}
	}

	static bool deserializeValueBinary( void* valuePtr, const hashed_string& typeName,
										const uint8* data, size_t dataSize, size_t& offset,
										const SerializeContext& ctx )
	{
		if ( const auto* reader = ctx.findBinaryReader( typeName ) )
		{
			return ( *reader )( valuePtr, data, dataSize, offset );
		}

		if ( const EnumInfo* enumInfo = sw::core::getTypeRegistry().findEnum( typeName ) )
		{
			(void)enumInfo;
			if ( offset + sizeof( int64 ) > dataSize )
				return false;
			std::memcpy( valuePtr, data + offset, sizeof( int64 ) );
			offset += sizeof( int64 );
			return true;
		}

		if ( const TypeInfo* structInfo = sw::core::getTypeRegistry().findType( typeName ) )
		{
			return BinarySerializer::deserialize( valuePtr, *structInfo, data + offset, dataSize - offset, ctx );
		}

		return false;
	}

	void BinarySerializer::serialize( const void* instance, const TypeInfo& typeInfo, std::vector<uint8>& outBuffer,
									  const SerializeContext& ctx )
	{
		uint32		 propCount	= static_cast<uint32>( typeInfo._propertyList.size() );
		const uint8* countBytes = reinterpret_cast<const uint8*>( &propCount );
		outBuffer.insert( outBuffer.end(), countBytes, countBytes + sizeof( uint32 ) );

		for ( const PropertyInfo& prop : typeInfo._propertyList )
		{
			const void* propPtr = prop.getValuePtr<void>( instance );

			uint32		 hashVal   = prop.getNameHash();
			const uint8* hashBytes = reinterpret_cast<const uint8*>( &hashVal );
			outBuffer.insert( outBuffer.end(), hashBytes, hashBytes + sizeof( uint32 ) );

			size_t		 sizeHeaderPos = outBuffer.size();
			uint32		 dummySize	   = 0;
			const uint8* dummyBytes	   = reinterpret_cast<const uint8*>( &dummySize );
			outBuffer.insert( outBuffer.end(), dummyBytes, dummyBytes + sizeof( uint32 ) );

			size_t payloadStart = outBuffer.size();

			if ( prop._bIsContainer && prop._containerWrapper )
			{
				if ( ISequenceContainerWrapper* seq = prop._containerWrapper->asSequence() )
				{
					uint32		 count	= static_cast<uint32>( seq->getSize( propPtr ) );
					const uint8* cBytes = reinterpret_cast<const uint8*>( &count );
					outBuffer.insert( outBuffer.end(), cBytes, cBytes + sizeof( uint32 ) );

					for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
					{
						const void* elemPtr = seq->getElementConst( propPtr, elemIndex );
						serializeValueBinary( elemPtr, prop._elementTypeName, outBuffer, ctx );
					}
				}
				else if ( IMapContainerWrapper* mapWrap = prop._containerWrapper->asMap() )
				{
					uint32		 count	= static_cast<uint32>( mapWrap->getSize( propPtr ) );
					const uint8* cBytes = reinterpret_cast<const uint8*>( &count );
					outBuffer.insert( outBuffer.end(), cBytes, cBytes + sizeof( uint32 ) );

					mapWrap->forEach( propPtr, [&]( const void* kPtr, const void* vPtr )
					{
						serializeValueBinary( kPtr, prop._keyTypeName, outBuffer, ctx );
						serializeValueBinary( vPtr, prop._elementTypeName, outBuffer, ctx );
					} );
				}
			}
			else
			{
				serializeValueBinary( propPtr, prop._typeName, outBuffer, ctx );
			}

			uint32 payloadSize = static_cast<uint32>( outBuffer.size() - payloadStart );
			std::memcpy( &outBuffer[sizeHeaderPos], &payloadSize, sizeof( uint32 ) );
		}
	}

	bool BinarySerializer::deserialize( void* instance, const TypeInfo& typeInfo, const uint8* data, size_t dataSize,
										const SerializeContext& ctx )
	{
		size_t offset = 0;
		if ( offset + sizeof( uint32 ) > dataSize )
			return false;

		uint32 propCount = 0;
		std::memcpy( &propCount, data + offset, sizeof( uint32 ) );
		offset += sizeof( uint32 );

		for ( uint32 index = 0; index < propCount; ++index )
		{
			if ( offset + sizeof( uint32 ) * 2 > dataSize )
				return false;

			uint32 tagHash	   = 0;
			uint32 payloadSize = 0;
			std::memcpy( &tagHash, data + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );

			std::memcpy( &payloadSize, data + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );

			if ( offset + payloadSize > dataSize )
				return false;

			const PropertyInfo* targetProp = nullptr;
			for ( const PropertyInfo& prop : typeInfo._propertyList )
			{
				if ( prop.getNameHash() == tagHash || ( prop._alias.empty() == false && prop.getAliasHash() == tagHash ) )
				{
					targetProp = &prop;
					break;
				}
			}

			if ( targetProp == nullptr )
			{
				offset += payloadSize;
				continue;
			}

			const PropertyInfo& prop	= *targetProp;
			void*				propPtr = prop.getValuePtr<void>( instance );

			if ( prop._bIsContainer && prop._containerWrapper )
			{
				prop._containerWrapper->clear( propPtr );

				if ( ISequenceContainerWrapper* seq = prop._containerWrapper->asSequence() )
				{
					if ( offset + sizeof( uint32 ) > dataSize )
						return false;
					uint32 count = 0;
					std::memcpy( &count, data + offset, sizeof( uint32 ) );
					offset += sizeof( uint32 );

					seq->reserve( propPtr, count );

					for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
					{
						seq->addElementDefault( propPtr );
						void* elemPtr = seq->getElement( propPtr, elemIndex );
						if ( deserializeValueBinary( elemPtr, prop._elementTypeName, data, dataSize, offset, ctx ) == false )
							return false;
					}
				}
				else if ( IMapContainerWrapper* mapWrap = prop._containerWrapper->asMap() )
				{
					if ( offset + sizeof( uint32 ) > dataSize )
						return false;
					uint32 count = 0;
					std::memcpy( &count, data + offset, sizeof( uint32 ) );
					offset += sizeof( uint32 );

					std::vector<uint8> kBuf( mapWrap->getKeySize() );
					std::vector<uint8> vBuf( mapWrap->getValueSize() );

					for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
					{
						mapWrap->defaultConstructKey( kBuf.data() );
						mapWrap->defaultConstructValue( vBuf.data() );

						bool kOk = deserializeValueBinary( kBuf.data(), prop._keyTypeName, data, dataSize, offset, ctx );
						bool vOk = deserializeValueBinary( vBuf.data(), prop._elementTypeName, data, dataSize, offset, ctx );

						if ( kOk && vOk )
						{
							mapWrap->insertKeyValue( propPtr, kBuf.data(), vBuf.data() );
						}

						mapWrap->destroyKey( kBuf.data() );
						mapWrap->destroyValue( vBuf.data() );

						if ( kOk == false || vOk == false )
							return false;
					}
				}
			}
			else
			{
				if ( deserializeValueBinary( propPtr, prop._typeName, data, dataSize, offset, ctx ) == false )
					return false;
			}
		}

		return true;
	}

	void BinarySerializer::serializeVersioned( uint32 version, const void* instance, const TypeInfo& typeInfo, std::vector<uint8>& outBuffer,
											   const SerializeContext& ctx )
	{
		const uint8* verBytes = reinterpret_cast<const uint8*>( &version );
		outBuffer.insert( outBuffer.end(), verBytes, verBytes + sizeof( uint32 ) );
		serialize( instance, typeInfo, outBuffer, ctx );
	}

	bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* instance, const TypeInfo& typeInfo, const uint8* data, size_t dataSize,
												 const SerializeContext& ctx )
	{
		if ( data == nullptr || dataSize < sizeof( uint32 ) )
			return false;

		std::memcpy( &outVersion, data, sizeof( uint32 ) );
		return deserialize( instance, typeInfo, data + sizeof( uint32 ), dataSize - sizeof( uint32 ), ctx );
	}

	bool BinarySerializer::cloneObject( void* dstData, const void* srcData, const TypeInfo& typeInfo )
	{
		if ( dstData == nullptr || srcData == nullptr )
			return false;

		if ( typeInfo._size > 0 && typeInfo.isPODFastPath() )
		{
			std::memcpy( dstData, srcData, typeInfo._size );
			return true;
		}

		thread_local std::vector<uint8> s_cloneBuffer;
		s_cloneBuffer.clear();
		const size_t targetCapacity = typeInfo._size > 0 ? typeInfo._size * 2 : 256;
		if ( s_cloneBuffer.capacity() < targetCapacity )
			s_cloneBuffer.reserve( targetCapacity );

		serialize( srcData, typeInfo, s_cloneBuffer );
		if ( s_cloneBuffer.empty() )
			return false;

		return deserialize( dstData, typeInfo, s_cloneBuffer.data(), s_cloneBuffer.size() );
	}

	static void valueToJson( sw::StringBuilder<sw::constant::kMaxBuffer8192>& ss, const void* valPtr, const hashed_string& typeName,
							 const SerializeContext& ctx )
	{
		if ( const auto* textWriter = ctx.findTextWriter( typeName ) )
		{
			std::string valStr = ( *textWriter )( valPtr );
			if ( typeName == hashed_string( "std::string" ) || typeName == hashed_string( "string" ) )
			{
				ss.append( "\"" );
				ss.append( valStr.c_str() );
				ss.append( "\"" );
			}
			else
				ss.append( valStr.c_str() );
			return;
		}

		if ( const EnumInfo* enumInfo = sw::core::getTypeRegistry().findEnum( typeName ) )
		{
			int64 val = *static_cast<const int64*>( valPtr );
			ss.append( "\"" );
			if ( enumInfo->_bIsBitFlag )
				ss.append( enumInfo->toStringFlags( val ).c_str() );
			else
				ss.append( enumInfo->toString( val ).c_str() );
			ss.append( "\"" );
			return;
		}

		if ( const TypeInfo* structInfo = sw::core::getTypeRegistry().findType( typeName ) )
		{
			ss.append( JsonSerializer::serialize( valPtr, *structInfo, ctx ) );
			return;
		}

		ss.append( "null" );
	}

	std::string JsonSerializer::serialize( const void* instance, const TypeInfo& typeInfo,
										   const SerializeContext& ctx )
	{
		sw::StringBuilder<8192> ss;
		ss.append( "{" );

		bool first = true;
		for ( const PropertyInfo& prop : typeInfo._propertyList )
		{
			if ( first == false )
				ss.append( "," );
			first = false;

			ss.append( "\"" );
			ss.append( prop._name.c_str() );
			ss.append( "\":" );
			const void* propPtr = prop.getValuePtr<void>( instance );

			if ( prop._bIsContainer && prop._containerWrapper )
			{
				if ( ISequenceContainerWrapper* seq = prop._containerWrapper->asSequence() )
				{
					ss.append( "[" );
					size_t sz = seq->getSize( propPtr );
					for ( size_t elemIndex = 0; elemIndex < sz; ++elemIndex )
					{
						if ( elemIndex > 0 )
							ss.append( "," );
						const void* elemPtr = seq->getElementConst( propPtr, elemIndex );
						valueToJson( ss, elemPtr, prop._elementTypeName, ctx );
					}
					ss.append( "]" );
				}
				else if ( IMapContainerWrapper* mapWrap = prop._containerWrapper->asMap() )
				{
					ss.append( "{" );
					bool mapFirst = true;
					mapWrap->forEach( propPtr, [&]( const void* kPtr, const void* vPtr )
					{
						if ( mapFirst == false )
							ss.append( "," );
						mapFirst = false;

						sw::StringBuilder<8192> keySs;
						valueToJson( keySs, kPtr, prop._keyTypeName, ctx );
						std::string_view keyStr = keySs.view();
						if ( keyStr.empty() == false && keyStr.front() == '"' )
						{
							ss.append( keyStr );
							ss.append( ":" );
						}
						else
						{
							ss.append( "\"" );
							ss.append( keyStr );
							ss.append( "\":" );
						}

						valueToJson( ss, vPtr, prop._elementTypeName, ctx );
					} );
					ss.append( "}" );
				}
			}
			else
			{
				valueToJson( ss, propPtr, prop._typeName, ctx );
			}
		}

		ss.append( "}" );
		return std::string( ss.view() );
	}

	std::string JsonSerializer::serializePretty( const void* instance, const TypeInfo& typeInfo, uint32 indentSpaces,
												 const SerializeContext& ctx )
	{
		std::string compactJson = serialize( instance, typeInfo, ctx );
		std::string prettyJson;
		prettyJson.reserve( compactJson.size() * 2 );

		uint32 currentIndent = 0;
		bool   bInQuotes	 = false;

		for ( size_t i = 0; i < compactJson.size(); ++i )
		{
			char ch = compactJson[i];
			if ( ch == '"' && ( i == 0 || compactJson[i - 1] != '\\' ) )
			{
				bInQuotes = bInQuotes == false;
				prettyJson.push_back( ch );
			}
			else if ( bInQuotes == true )
			{
				prettyJson.push_back( ch );
			}
			else if ( ch == '{' || ch == '[' )
			{
				prettyJson.push_back( ch );
				prettyJson.push_back( '\n' );
				currentIndent += indentSpaces;
				prettyJson.append( currentIndent, ' ' );
			}
			else if ( ch == '}' || ch == ']' )
			{
				prettyJson.push_back( '\n' );
				if ( currentIndent >= indentSpaces )
					currentIndent -= indentSpaces;
				prettyJson.append( currentIndent, ' ' );
				prettyJson.push_back( ch );
			}
			else if ( ch == ',' )
			{
				prettyJson.push_back( ch );
				prettyJson.push_back( '\n' );
				prettyJson.append( currentIndent, ' ' );
			}
			else if ( ch == ':' )
			{
				prettyJson.push_back( ch );
				prettyJson.push_back( ' ' );
			}
			else
			{
				prettyJson.push_back( ch );
			}
		}

		return prettyJson;
	}

	static bool parseTextValue( void* valPtr, const hashed_string& typeName, std::string_view valStr,
								const SerializeContext& ctx )
	{
		if ( const auto* textReader = ctx.findTextReader( typeName ) )
		{
			return ( *textReader )( valPtr, valStr );
		}

		if ( const EnumInfo* enumInfo = sw::core::getTypeRegistry().findEnum( typeName ) )
		{
			std::string s( valStr );
			if ( s.empty() == false && s.front() == '"' )
				s = s.substr( 1, s.size() - 2 );
			int64 v						   = enumInfo->stringFlagsToValue( s );
			*static_cast<int64*>( valPtr ) = v;
			return true;
		}

		return false;
	}

	bool JsonSerializer::deserialize( void* instance, const TypeInfo& typeInfo, std::string_view jsonStr,
									  const SerializeContext& ctx )
	{
		std::string		 trimmedJsonStr = StringUtil::trim( jsonStr );
		std::string_view trimmedJson{ trimmedJsonStr };
		if ( trimmedJson.empty() || trimmedJson.front() != '{' || trimmedJson.back() != '}' )
			return false;

		std::string s{ trimmedJson };

		for ( const PropertyInfo& prop : typeInfo._propertyList )
		{
			std::string keyPattern = "\"" + std::string( prop._name.c_str() ) + "\"";
			size_t		keyPos	   = s.find( keyPattern );
			if ( keyPos == std::string::npos && prop._alias.empty() == false )
			{
				keyPattern = "\"" + std::string( prop._alias.c_str() ) + "\"";
				keyPos	   = s.find( keyPattern );
			}
			if ( keyPos == std::string::npos )
				continue;

			size_t colonPos = s.find( ':', keyPos + keyPattern.size() );
			if ( colonPos == std::string::npos )
				continue;

			void* propPtr = prop.getValuePtr<void>( instance );

			if ( prop._bIsContainer && prop._containerWrapper )
			{
				size_t arrayStart = s.find( '[', colonPos );
				size_t arrayEnd	  = s.find( ']', arrayStart );
				if ( arrayStart != std::string::npos && arrayEnd != std::string::npos )
				{
					std::string_view content = std::string_view{ s }.substr( arrayStart + 1, arrayEnd - arrayStart - 1 );
					prop._containerWrapper->clear( propPtr );

					if ( ISequenceContainerWrapper* seq = prop._containerWrapper->asSequence() )
					{
						string_splitter itemSplitter{ content, { "," } };
						const auto&		items = itemSplitter.getSplitList();
						seq->reserve( propPtr, items.size() );

						size_t idx = 0;
						for ( std::string_view rawItem : items )
						{
							std::string		 itemStr = StringUtil::trim( rawItem );
							std::string_view item{ itemStr };
							if ( item.empty() == false )
							{
								seq->addElementDefault( propPtr );
								void* elemPtr = seq->getElement( propPtr, idx++ );
								parseTextValue( elemPtr, prop._elementTypeName, item, ctx );
							}
						}
					}
				}
			}
			else
			{
				size_t valStart = colonPos + 1;
				while ( valStart < s.size() && std::isspace( static_cast<unsigned char>( s[valStart] ) ) )
					++valStart;

				size_t valEnd = valStart;
				if ( s[valStart] == '"' )
				{
					valEnd = s.find( '"', valStart + 1 );
					if ( valEnd != std::string::npos )
						valEnd += 1;
				}
				else
				{
					while ( valEnd < s.size() && s[valEnd] != ',' && s[valEnd] != '}' )
						++valEnd;
				}

				std::string		 valTokenStr = StringUtil::trim( std::string_view{ s }.substr( valStart, valEnd - valStart ) );
				std::string_view valToken{ valTokenStr };
				parseTextValue( propPtr, prop._typeName, valToken, ctx );
			}
		}

		return true;
	}

	struct RapidXmlBackend::Impl
	{
		rapidxml::xml_document<>		   doc;
		rapidxml::xml_node<>*			   currentParent = nullptr;
		std::vector<rapidxml::xml_node<>*> nodeStack;

		std::string xmlBuffer;

		static std::string sanitizeTag( const utf8* name )
		{
			std::string s( name ? name : "" );
			for ( size_t pos = 0; ( pos = s.find( "::", pos ) ) != std::string::npos; pos += 2 )
				s.replace( pos, 2, "__" );
			return s;
		}

		static void xmlNodeToString( std::string& out, const rapidxml::xml_node<>* node )
		{
			if ( node == nullptr )
				return;

			if ( node->type() == rapidxml::node_document )
			{
				for ( const rapidxml::xml_node<>* child = node->first_node(); child != nullptr; child = child->next_sibling() )
					xmlNodeToString( out, child );

				return;
			}

			out += '<';
			out += node->name();
			out += '>';

			const utf8* val = node->value();
			if ( val != nullptr && *val != '\0' )
				out += val;

			for ( const rapidxml::xml_node<>* child = node->first_node(); child != nullptr; child = child->next_sibling() )
				xmlNodeToString( out, child );

			out += "</";
			out += node->name();
			out += '>';
		}
	};

	RapidXmlBackend::RapidXmlBackend() : _impl{ std::make_unique<Impl>() } {}
	RapidXmlBackend::~RapidXmlBackend() = default;

	void RapidXmlBackend::initXmlSerialization( const utf8* rootTagName )
	{
		_impl->doc.clear();
		std::string			  tag	   = Impl::sanitizeTag( rootTagName );
		char*				  rootName = _impl->doc.allocate_string( tag.c_str() );
		rapidxml::xml_node<>* root	   = _impl->doc.allocate_node( rapidxml::node_element, rootName );
		_impl->doc.append_node( root );
		_impl->currentParent = root;
		_impl->nodeStack.push_back( root );
	}

	void RapidXmlBackend::writeValue( const utf8* tagName, const utf8* valueString )
	{
		if ( _impl->currentParent == nullptr )
			return;

		std::string			  sTag = Impl::sanitizeTag( tagName );
		char*				  name = _impl->doc.allocate_string( sTag.c_str() );
		char*				  val  = _impl->doc.allocate_string( valueString != nullptr ? valueString : "" );
		rapidxml::xml_node<>* node = _impl->doc.allocate_node( rapidxml::node_element, name, val );
		_impl->currentParent->append_node( node );
	}

	void RapidXmlBackend::beginArray( const utf8* tagName )
	{
		beginMap( tagName );
	}

	void RapidXmlBackend::writeArrayItem( const utf8* valueString )
	{
		writeValue( "item", valueString );
	}

	void RapidXmlBackend::endArray()
	{
		endMap();
	}

	void RapidXmlBackend::beginMap( const utf8* tagName )
	{
		if ( _impl->currentParent == nullptr )
			return;

		std::string			  sTag = Impl::sanitizeTag( tagName );
		char*				  name = _impl->doc.allocate_string( sTag.c_str() );
		rapidxml::xml_node<>* node = _impl->doc.allocate_node( rapidxml::node_element, name );
		_impl->currentParent->append_node( node );
		_impl->nodeStack.push_back( node );
		_impl->currentParent = node;
	}

	void RapidXmlBackend::beginMapEntry()
	{
		beginMap( "entry" );
	}

	void RapidXmlBackend::writeMapKey( const utf8* keyString )
	{
		writeValue( "key", keyString );
	}

	void RapidXmlBackend::writeMapValue( const utf8* valueString )
	{
		writeValue( "value", valueString );
	}

	void RapidXmlBackend::endMapEntry()
	{
		endMap();
	}

	void RapidXmlBackend::endMap()
	{
		if ( _impl->nodeStack.size() > 1 )
		{
			_impl->nodeStack.pop_back();
			_impl->currentParent = _impl->nodeStack.back();
		}
	}

	std::string RapidXmlBackend::endSerialize()
	{
		std::string result;
		Impl::xmlNodeToString( result, &_impl->doc );
		return result;
	}

	bool RapidXmlBackend::initXmlDeserialization( const utf8* xmlStr, const utf8* rootTagName )
	{
		_impl->doc.clear();
		_impl->xmlBuffer = xmlStr != nullptr ? xmlStr : "";
		if ( _impl->xmlBuffer.empty() )
			return false;

		_impl->doc.parse<0>( &_impl->xmlBuffer[0] );

		std::string			  sTag = Impl::sanitizeTag( rootTagName );
		rapidxml::xml_node<>* root = _impl->doc.first_node( sTag.c_str() );
		if ( root == nullptr )
			root = _impl->doc.first_node();

		if ( root == nullptr )
			return false;

		_impl->currentParent = root;
		return true;
	}

	bool RapidXmlBackend::readValue( const utf8* tagName, std::string& outValue )
	{
		if ( _impl->currentParent == nullptr )
			return false;

		std::string			  sTag = Impl::sanitizeTag( tagName );
		rapidxml::xml_node<>* node = _impl->currentParent->first_node( sTag.c_str() );
		if ( node == nullptr )
			return false;

		outValue = node->value() != nullptr ? node->value() : "";
		return true;
	}

	bool RapidXmlBackend::iterateArray( const utf8* tagName, const XmlArrayItemDelegate& callback )
	{
		if ( _impl->currentParent == nullptr )
			return false;

		std::string			  sTag	  = Impl::sanitizeTag( tagName );
		rapidxml::xml_node<>* arrNode = _impl->currentParent->first_node( sTag.c_str() );
		if ( arrNode == nullptr )
			return false;

		for ( rapidxml::xml_node<>* item = arrNode->first_node( "item" ); item != nullptr; item = item->next_sibling( "item" ) )
			callback( item->value() != nullptr ? item->value() : "" );

		return true;
	}

	bool RapidXmlBackend::iterateMap( const utf8* tagName, const XmlMapItemDelegate& callback )
	{
		if ( _impl->currentParent == nullptr )
			return false;

		std::string			  sTag	  = Impl::sanitizeTag( tagName );
		rapidxml::xml_node<>* mapNode = _impl->currentParent->first_node( sTag.c_str() );
		if ( mapNode == nullptr )
			return false;

		for ( rapidxml::xml_node<>* entry = mapNode->first_node( "entry" ); entry != nullptr; entry = entry->next_sibling( "entry" ) )
		{
			rapidxml::xml_node<>* kNode = entry->first_node( "key" );
			rapidxml::xml_node<>* vNode = entry->first_node( "value" );
			if ( kNode != nullptr && vNode != nullptr )
				callback( kNode->value() != nullptr ? kNode->value() : "", vNode->value() != nullptr ? vNode->value() : "" );
		}
		return true;
	}

	std::string XmlSerializer::serialize( const void* instance, const TypeInfo& typeInfo,
										  IXmlBackend& backend, const SerializeContext& ctx )
	{
		backend.initXmlSerialization( typeInfo._name.c_str() );

		for ( const PropertyInfo& prop : typeInfo._propertyList )
		{
			const void* propPtr = prop.getValuePtr<void>( instance );

			if ( prop._bIsContainer && prop._containerWrapper )
			{
				if ( ISequenceContainerWrapper* seq = prop._containerWrapper->asSequence() )
				{
					backend.beginArray( prop._name.c_str() );
					size_t sz = seq->getSize( propPtr );
					for ( size_t elemIndex = 0; elemIndex < sz; ++elemIndex )
					{
						const void*									elemPtr = seq->getElementConst( propPtr, elemIndex );
						sw::StringBuilder<constant::kMaxBuffer8192> ss;
						valueToJson( ss, elemPtr, prop._elementTypeName, ctx );
						std::string_view valStr = ss.view();
						if ( valStr.empty() == false && valStr.front() == '"' )
							valStr = valStr.substr( 1, valStr.size() - 2 );
						backend.writeArrayItem( std::string( valStr ).c_str() );
					}
					backend.endArray();
				}
				else if ( IMapContainerWrapper* mapWrap = prop._containerWrapper->asMap() )
				{
					backend.beginMap( prop._name.c_str() );
					mapWrap->forEach( propPtr, [&]( const void* kPtr, const void* vPtr )
					{
						backend.beginMapEntry();

						sw::StringBuilder<constant::kMaxBuffer8192> kSs, vSs;
						valueToJson( kSs, kPtr, prop._keyTypeName, ctx );
						valueToJson( vSs, vPtr, prop._elementTypeName, ctx );
						std::string_view kStr = kSs.view();
						std::string_view vStr = vSs.view();
						if ( kStr.empty() == false && kStr.front() == '"' )
							kStr = kStr.substr( 1, kStr.size() - 2 );
						if ( vStr.empty() == false && vStr.front() == '"' )
							vStr = vStr.substr( 1, vStr.size() - 2 );

						backend.writeMapKey( std::string( kStr ).c_str() );
						backend.writeMapValue( std::string( vStr ).c_str() );
						backend.endMapEntry();
					} );
					backend.endMap();
				}
			}
			else
			{
				sw::StringBuilder<constant::kMaxBuffer8192> ss;
				valueToJson( ss, propPtr, prop._typeName, ctx );
				std::string_view valStr = ss.view();
				if ( valStr.empty() == false && valStr.front() == '"' )
					valStr = valStr.substr( 1, valStr.size() - 2 );
				backend.writeValue( prop._name.c_str(), std::string( valStr ).c_str() );
			}
		}

		return backend.endSerialize();
	}

	bool XmlSerializer::deserialize( void* instance, const TypeInfo& typeInfo,
									 IXmlBackend& backend, std::string_view xmlStr,
									 const SerializeContext& ctx )
	{
		std::string xmlBuffer( xmlStr );
		if ( backend.initXmlDeserialization( xmlBuffer.c_str(), typeInfo._name.c_str() ) == false )
			return false;

		for ( const PropertyInfo& prop : typeInfo._propertyList )
		{
			void* propPtr = prop.getValuePtr<void>( instance );

			if ( prop._bIsContainer && prop._containerWrapper )
			{
				prop._containerWrapper->clear( propPtr );

				if ( ISequenceContainerWrapper* seq = prop._containerWrapper->asSequence() )
				{
					size_t idx = 0;
					backend.iterateArray( prop._name.c_str(), [&]( std::string_view itemStr )
					{
						seq->addElementDefault( propPtr );
						void* elemPtr = seq->getElement( propPtr, idx++ );
						parseTextValue( elemPtr, prop._elementTypeName, itemStr, ctx );
					} );
				}
				else if ( IMapContainerWrapper* mapWrap = prop._containerWrapper->asMap() )
				{
					std::vector<uint8> kBuf( mapWrap->getKeySize() );
					std::vector<uint8> vBuf( mapWrap->getValueSize() );

					backend.iterateMap( prop._name.c_str(), [&]( std::string_view keyStr, std::string_view valStr )
					{
						mapWrap->defaultConstructKey( kBuf.data() );
						mapWrap->defaultConstructValue( vBuf.data() );

						bool kOk = parseTextValue( kBuf.data(), prop._keyTypeName, keyStr, ctx );
						bool vOk = parseTextValue( vBuf.data(), prop._elementTypeName, valStr, ctx );

						if ( kOk && vOk )
						{
							mapWrap->insertKeyValue( propPtr, kBuf.data(), vBuf.data() );
						}

						mapWrap->destroyKey( kBuf.data() );
						mapWrap->destroyValue( vBuf.data() );
					} );
				}
			}
			else
			{
				std::string valStr;
				if ( backend.readValue( prop._name.c_str(), valStr ) )
				{
					parseTextValue( propPtr, prop._typeName, valStr, ctx );
				}
			}
		}

		return true;
	}

	std::string XmlSerializer::serialize( const void* instance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		RapidXmlBackend defaultBackend;
		return serialize( instance, typeInfo, defaultBackend, ctx );
	}

	bool XmlSerializer::deserialize( void* instance, const TypeInfo& typeInfo, std::string_view xmlStr, const SerializeContext& ctx )
	{
		RapidXmlBackend defaultBackend;
		return deserialize( instance, typeInfo, defaultBackend, xmlStr, ctx );
	}

	bool ObjectDiffSerializer::serializeDiff( std::vector<uint8>& outDiffBuffer, const void* cdoInstance, const void* modifiedInstance, const TypeInfo& typeInfo )
	{
		if ( cdoInstance == nullptr || modifiedInstance == nullptr )
			return false;

		outDiffBuffer.clear();

		for ( const PropertyInfo& prop : typeInfo._propertyList )
		{
			const void* cdoPtr = prop.getValuePtr<void>( cdoInstance );
			const void* modPtr = prop.getValuePtr<void>( modifiedInstance );

			bool bIsDifferent = false;
			if ( prop._bIsContainer )
			{
				bIsDifferent = true;
			}
			else
			{
				bIsDifferent = std::memcmp( cdoPtr, modPtr, sizeof( float4x4 ) < 64 ? 64 : sizeof( float4x4 ) ) != 0;
			}

			if ( bIsDifferent )
			{
				uint32		 nameHash  = prop.getNameHash();
				const uint8* hashBytes = reinterpret_cast<const uint8*>( &nameHash );
				outDiffBuffer.insert( outDiffBuffer.end(), hashBytes, hashBytes + sizeof( uint32 ) );
			}
		}

		return true;
	}

	bool ObjectDiffSerializer::deserializeDiff( void* targetInstance, const TypeInfo& typeInfo, const uint8* diffData, size_t diffSize )
	{
		(void)targetInstance;
		(void)typeInfo;
		(void)diffData;
		(void)diffSize;
		// serializeDiff currently records only property name hashes (no payload), so apply is not implemented.
		SW_LOG_ERROR( "[ObjectDiffSerializer] deserializeDiff is not implemented (diff buffer has no property payloads)." );
		return false;
	}

} // namespace sw
