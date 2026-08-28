#include "pch.h"

#include "Engine/Reflection/ReflectAny.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{

	namespace
	{

		void writeReflectAnyBinary( const void* pPtr, vector<uint8>& listBuf )
		{
			const ReflectAny& any		 = *static_cast<const ReflectAny*>( pPtr );
			const string_view fqn		 = any._typeFqn.view();
			const uint32	  nameLen	 = static_cast<uint32>( fqn.size() );
			const uint32	  blobLen	 = static_cast<uint32>( any._listBytes.size() );
			const uint8*	  pNameBytes = reinterpret_cast<const uint8*>( &nameLen );
			const uint8*	  pBlobBytes = reinterpret_cast<const uint8*>( &blobLen );
			listBuf.insert( listBuf.end(), pNameBytes, pNameBytes + sizeof( uint32 ) );
			listBuf.insert( listBuf.end(), reinterpret_cast<const uint8*>( fqn.data() ),
							reinterpret_cast<const uint8*>( fqn.data() ) + fqn.size() );
			listBuf.insert( listBuf.end(), pBlobBytes, pBlobBytes + sizeof( uint32 ) );
			listBuf.insert( listBuf.end(), any._listBytes.begin(), any._listBytes.end() );
		}

		bool readReflectAnyBinary( void* pPtr, const uint8* pData, size_t size, size_t& offset )
		{
			ReflectAny& any = *static_cast<ReflectAny*>( pPtr );
			any				= ReflectAny{};
			if ( offset + sizeof( uint32 ) > size )
				return false;
			uint32 nameLen{ 0 };
			Memory::copy( &nameLen, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );
			if ( offset + nameLen > size )
				return false;
			any._typeFqn = hashed_string{ reinterpret_cast<const utf8*>( pData + offset ), nameLen };
			offset += nameLen;
			if ( offset + sizeof( uint32 ) > size )
				return false;
			uint32 blobLen{ 0 };
			Memory::copy( &blobLen, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );
			if ( offset + blobLen > size )
				return false;
			any._listBytes.assign( pData + offset, pData + offset + blobLen );
			offset += blobLen;
			return true;
		}

		string writeReflectAnyText( const void* pPtr )
		{
			const ReflectAny& any = *static_cast<const ReflectAny*>( pPtr );
			// Compact JSON: {"t":"fqn","b":"hex..."} — keep simple: type|base64-ish hex
			string hex;
			hex.reserve( any._listBytes.size() * 2 );
			static constexpr const utf8* kDigits = "0123456789abcdef";
			for ( uint8 byte : any._listBytes )
			{
				hex.push_back( kDigits[( byte >> 4 ) & 0xF] );
				hex.push_back( kDigits[byte & 0xF] );
			}
			StringBuilder<128> sb;
			sb.append( any._typeFqn.view() ).append( '|' ).append( hex );
			return string{ sb.c_str(), sb.size() };
		}

		bool readReflectAnyText( void* pPtr, string_view str )
		{
			ReflectAny& any	 = *static_cast<ReflectAny*>( pPtr );
			any				 = ReflectAny{};
			const size_t bar = str.find( '|' );
			if ( bar == string_view::npos )
				return false;
			any._typeFqn		  = hashed_string{ str.substr( 0, bar ) };
			const string_view hex = str.substr( bar + 1 );
			if ( ( hex.size() % 2 ) != 0 )
				return false;
			any._listBytes.resize( hex.size() / 2 );
			const auto nibble = []( utf8 ch ) -> int32
			{
				if ( '0' <= ch && ch <= '9' )
					return ch - '0';
				if ( 'a' <= ch && ch <= 'f' )
					return ch - 'a' + 10;
				if ( 'A' <= ch && ch <= 'F' )
					return ch - 'A' + 10;
				return -1;
			};
			for ( size_t byteIndex = 0; byteIndex < any._listBytes.size(); ++byteIndex )
			{
				const int32 hiNibble = nibble( hex[byteIndex * 2] );
				const int32 loNibble = nibble( hex[byteIndex * 2 + 1] );
				if ( hiNibble < 0 || loNibble < 0 )
					return false;
				any._listBytes[byteIndex] = static_cast<uint8>( ( hiNibble << 4 ) | loNibble );
			}
			return true;
		}

	} // namespace

	ReflectAny ReflectAny::makeFrom( const TypeInfo& info, const void* pValue )
	{
		ReflectAny any;
		if ( pValue == nullptr )
			return any;
		any._typeFqn = info._fullyQualifiedName.empty() == false ? info._fullyQualifiedName : info._name;
		BinarySerializer::serialize( pValue, info, any._listBytes );
		return any;
	}

	bool ReflectAny::tryGetFrom( const TypeInfo& info, void* pOut ) const
	{
		if ( pOut == nullptr || empty() )
			return false;
		const hashed_string& want =
			info._fullyQualifiedName.empty() == false ? info._fullyQualifiedName : info._name;
		if ( _typeFqn != want )
			return false;
		return BinarySerializer::deserialize( pOut, info, _listBytes.data(), _listBytes.size() );
	}

	void registerReflectAnyHandlers( SerializeContext& ctx )
	{
		const hashed_string typeName( "sw::ReflectAny" );
		ctx.registerBinaryHandler( typeName, SW_DELEGATE_FUNCTION( SerializeContext::BinaryWriteFn, writeReflectAnyBinary ),
								   SW_DELEGATE_FUNCTION( SerializeContext::BinaryReadFn, readReflectAnyBinary ) );
		ctx.registerTextHandler( typeName, SW_DELEGATE_FUNCTION( SerializeContext::TextWriteFn, writeReflectAnyText ),
								 SW_DELEGATE_FUNCTION( SerializeContext::TextReadFn, readReflectAnyText ) );
		ctx.registerBinaryHandler( hashed_string( "ReflectAny" ),
								   SW_DELEGATE_FUNCTION( SerializeContext::BinaryWriteFn, writeReflectAnyBinary ),
								   SW_DELEGATE_FUNCTION( SerializeContext::BinaryReadFn, readReflectAnyBinary ) );
		ctx.registerTextHandler( hashed_string( "ReflectAny" ),
								 SW_DELEGATE_FUNCTION( SerializeContext::TextWriteFn, writeReflectAnyText ),
								 SW_DELEGATE_FUNCTION( SerializeContext::TextReadFn, readReflectAnyText ) );
	}
} // namespace sw
