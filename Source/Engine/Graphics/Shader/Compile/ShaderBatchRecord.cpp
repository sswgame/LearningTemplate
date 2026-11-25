#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderBatchRecord.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/ByteStream.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ShaderBatchRecordInternal
        {
            static constexpr uint32 kFormatVersion = 1;
            /** @brief 입력 하나가 파일에서 차지하는 최소 바이트입니다(종류 1 + 글 길이 둘 8 + 해시 8). */
            static constexpr size_t kMinInputBytes = 17;
            /** @brief 동작 하나가 파일에서 차지하는 바이트입니다(키 8 + 해시 8 + 번호 4). */
            static constexpr size_t kEntryBytes = 20;

            static uint64 computeListHash( const vector<CookInput>& listInput )
            {
                uint64 hash = HashUtil::kFNVOffset64;
                for ( const CookInput& input : listInput )
                {
                    hash = HashUtil::computeStringHash64( input.makeSourceKey(), false, hash );
                    hash = HashUtil::appendBytes64( hash, reinterpret_cast<const uint8*>( &input._contentHash ), sizeof( input._contentHash ) );
                }
                return hash;
            }

            [[nodiscard]] static bool readInput( ByteReader& reader, CookInput& outInput )
            {
                const uint8 kind = reader.readUint8();
                if ( kind > static_cast<uint8>( CookInputKind::Listing ) )
                    return false;
                outInput._kind        = static_cast<CookInputKind>( kind );
                outInput._source      = reader.readString();
                outInput._extension   = reader.readString();
                outInput._contentHash = reader.readUint64();
                return reader.hasOverflowed() == false;
            }
        };
    } // namespace

    ShaderBatchRecord::ShaderBatchRecord()
        : _listInputList{}
        , _mapInputListIndex{}
        , _mapEntry{}
    {
    }

    void ShaderBatchRecord::add( uint64 actionKey, uint64 bytecodeHash, const vector<CookInput>& listInput )
    {
        const auto [iter, bNew] = _mapInputListIndex.emplace( ShaderBatchRecordInternal::computeListHash( listInput ), static_cast<uint32>( _listInputList.size() ) );
        uint32 index            = iter->second;
        if ( bNew == false && ( _listInputList[index] == listInput ) == false )
            index = static_cast<uint32>( _listInputList.size() ); // 해시가 겹친 다른 목록 — 따로 둔다
        if ( index == _listInputList.size() )
            _listInputList.push_back( listInput );
        _mapEntry.insert_or_assign( actionKey, Entry{ bytecodeHash, index } );
    }

    const ShaderBatchRecord::Entry* ShaderBatchRecord::find( uint64 actionKey ) const
    {
        const auto iter = _mapEntry.find( actionKey );
        return iter != _mapEntry.end() ? &iter->second : nullptr;
    }

    void ShaderBatchRecord::write( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        ByteWriter writer{ outBytes };
        writer.writeUint32( ShaderBatchRecordInternal::kFormatVersion );
        writer.writeUint32( static_cast<uint32>( _listInputList.size() ) );
        for ( const vector<CookInput>& listInput : _listInputList )
        {
            writer.writeUint32( static_cast<uint32>( listInput.size() ) );
            for ( const CookInput& input : listInput )
            {
                writer.writeUint8( static_cast<uint8>( input._kind ) );
                writer.writeString( input._source );
                writer.writeString( input._extension );
                writer.writeUint64( input._contentHash );
            }
        }
        vector<uint64> listActionKey;
        listActionKey.reserve( _mapEntry.size() );
        for ( const auto& [actionKey, entry] : _mapEntry )
        {
            listActionKey.push_back( actionKey );
        }
        VectorUtil::sort( listActionKey );
        writer.writeUint32( static_cast<uint32>( listActionKey.size() ) );
        for ( const uint64 actionKey : listActionKey )
        {
            const Entry& entry = *find( actionKey );
            writer.writeUint64( actionKey );
            writer.writeUint64( entry._bytecodeHash );
            writer.writeUint32( entry._inputListIndex );
        }
    }

    bool ShaderBatchRecord::read( const vector<uint8>& bytes )
    {
        using Internal = ShaderBatchRecordInternal;
        _listInputList.clear();
        _mapInputListIndex.clear();
        _mapEntry.clear();
        ByteReader reader{ bytes.data(), bytes.size() };
        if ( reader.readUint32() != Internal::kFormatVersion )
            return false;
        const bool   bLists     = reader.readCountedList( _listInputList, sizeof( uint32 ), [&reader]( vector<CookInput>& outList )
              { return reader.readCountedList( outList, Internal::kMinInputBytes, [&reader]( CookInput& outInput )
                { return Internal::readInput( reader, outInput ); } ); } );
        const uint32 entryCount = bLists ? reader.readCount( Internal::kEntryBytes ) : 0;
        for ( uint32 index = 0; index < entryCount; ++index )
        {
            const uint64 actionKey = reader.readUint64();
            Entry        entry;
            entry._bytecodeHash   = reader.readUint64();
            entry._inputListIndex = reader.readUint32();
            if ( entry._inputListIndex >= _listInputList.size() )
                return false;
            _mapEntry.emplace( actionKey, entry );
        }
        return bLists && reader.isFullyRead();
    }

    string ShaderBatchRecord::makeKeyDescription( vector<uint64> listActionKey )
    {
        VectorUtil::sort( listActionKey );
        listActionKey.erase( std::unique( listActionKey.begin(), listActionKey.end() ), listActionKey.end() );
        uint64 hash = HashUtil::kFNVOffset64;
        for ( const uint64 actionKey : listActionKey )
        {
            hash = HashUtil::appendBytes64( hash, reinterpret_cast<const uint8*>( &actionKey ), sizeof( actionKey ) );
        }
        string description;
        description.append( "Shader batch record\ncount=" ).append( to_string( listActionKey.size() ) ).append( "\nactions=" ).append( to_string( hash ) ).append( "\n" );
        return description;
    }
} // namespace sw
