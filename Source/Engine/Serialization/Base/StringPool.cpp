#include "pch.h"

#include "Engine/Serialization/Base/StringPool.h"

#include "Core/Container/VarIntUtil.h"

#include "Engine/Serialization/Base/BinaryStream.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    StringPool::StringPool()
        : _listString{}
        , _mapStringToID{}
    {
        initializePredefined();
    }

    void StringPool::initializePredefined()
    {
        _listString.clear();
        _mapStringToID.clear();
        _listString.reserve( kPredefinedCount + 32 );
        _mapStringToID.reserve( kPredefinedCount + 32 );

#define REGISTER_NAME( index, name )                               \
    {                                                              \
        const string strKey        = #name;                        \
        const uint32 expectedIndex = static_cast<uint32>( index ); \
        _listString.push_back( strKey );                           \
        _mapStringToID.emplace( strKey, expectedIndex );           \
    }
#include "Core/Predefined/PredefinedNameType.xxx"
#undef REGISTER_NAME
    }

    uint32 StringPool::internString( string_view str )
    {
        const hashed_string hashed( str );
        const uint32        keyIndex = hashed.getIndex();
        if ( keyIndex < kPredefinedCount )
            return keyIndex;

        const string key( str );
        const auto   it = _mapStringToID.find( key );
        if ( it != _mapStringToID.end() )
            return it->second;

        const uint32 newID = static_cast<uint32>( _listString.size() );
        _listString.push_back( key );
        _mapStringToID.emplace( _listString.back(), newID );
        return newID;
    }

    string_view StringPool::getString( uint32 index ) const
    {
        if ( index >= _listString.size() )
            return {};
        return _listString[index];
    }

    void StringPool::clear()
    {
        initializePredefined();
    }

    void StringPool::saveToArchive( Archive& outArchive ) const
    {
        const size_t dynamicCount = getDynamicCount();
        outArchive.writeVarUint( static_cast<uint64>( dynamicCount ) );
        for ( size_t index = kPredefinedCount; index < _listString.size(); ++index )
        {
            outArchive.writeString( _listString[index] );
        }
    }

    bool StringPool::loadFromArchive( Archive& inArchive )
    {
        initializePredefined();
        uint64 dynamicCount = 0;
        if ( inArchive.readVarUint( dynamicCount ) == false || dynamicCount > kMaxDynamicStrings )
            return false;

        _listString.reserve( kPredefinedCount + static_cast<size_t>( dynamicCount ) );
        _mapStringToID.reserve( kPredefinedCount + static_cast<size_t>( dynamicCount ) );

        for ( uint64 strIndex = 0; strIndex < dynamicCount; ++strIndex )
        {
            string str;
            if ( inArchive.readString( str ) == false )
                return false;
            const uint32 stringID = static_cast<uint32>( _listString.size() );
            _listString.push_back( std::move( str ) );
            _mapStringToID.emplace( _listString.back(), stringID );
        }
        return true;
    }

    void StringPool::saveToBinaryBuffer( vector<uint8>& outBytes ) const
    {
        BinaryStreamWriter writer( outBytes );
        const size_t       dynamicCount = getDynamicCount();
        writer.writeVarUint( static_cast<uint64>( dynamicCount ) );
        for ( size_t index = kPredefinedCount; index < _listString.size(); ++index )
        {
            writer.writeString( _listString[index] );
        }
    }

    bool StringPool::loadFromBinaryBuffer( const uint8* pData, size_t dataSize, size_t& inoutOffset )
    {
        initializePredefined();
        uint64 dynamicCount = 0;
        if ( VarIntUtil::decodeVarUint64( pData, dataSize, inoutOffset, dynamicCount ) == false || dynamicCount > kMaxDynamicStrings )
            return false;

        _listString.reserve( kPredefinedCount + static_cast<size_t>( dynamicCount ) );
        _mapStringToID.reserve( kPredefinedCount + static_cast<size_t>( dynamicCount ) );

        for ( uint64 strIndex = 0; strIndex < dynamicCount; ++strIndex )
        {
            if ( inoutOffset + sizeof( uint32 ) > dataSize )
                return false;
            uint32 len = 0;
            Memory::copy( &len, pData + inoutOffset, sizeof( uint32 ) );
            inoutOffset += sizeof( uint32 );

            if ( inoutOffset + len > dataSize )
                return false;
            string str( reinterpret_cast<const utf8*>( pData + inoutOffset ), len );
            inoutOffset += len;

            const uint32 id = static_cast<uint32>( _listString.size() );
            _listString.push_back( str );
            _mapStringToID.emplace( std::move( str ), id );
        }
        return true;
    }
} // namespace sw
