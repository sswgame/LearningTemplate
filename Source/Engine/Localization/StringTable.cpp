#include "pch.h"

#include "Engine/Localization/StringTable.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Hash/HashUtil.h"

namespace sw
{
    const utf8* LocalizedTextArena::store( string_view text )
    {
        const uint64      contentHash = HashUtil::computeStringHash64( text );
        ScopedLock<mutex> lock{ _mutex };
        const auto        it = _mapByContent.find( contentHash );
        if ( it != _mapByContent.end() && string_view{ it->second } == text )
            return it->second;

        const size_t bytes = text.size() + 1;
        if ( _pCursor == nullptr || bytes > _remainingBytes )
        {
            const size_t blockBytes = bytes > kBlockBytes ? bytes : kBlockBytes;
            MemoryBlock  block      = MemoryBlock::allocate( blockBytes );
            _pCursor                = block.getAs<utf8>();
            _remainingBytes         = blockBytes;
            _listBlock.push_back( std::move( block ) );
        }
        utf8* const pStored = _pCursor;
        Memory::copy( pStored, text.data(), text.size() );
        pStored[text.size()] = '\0';
        _pCursor += bytes;
        _remainingBytes -= bytes;
        // 해시가 같은 다른 내용이면 먼저 둔 것을 남긴다(다음에 같은 내용이 오면 새로 둔다. 드문 경우다).
        if ( it == _mapByContent.end() )
            _mapByContent.emplace( contentHash, pStored );
        return pStored;
    }
} // namespace sw

namespace sw
{
    StringTable::StringTable( LocalizedTextArena& arena )
        : _arena{ arena }
        , _mutex{}
        , _mapTable{}
    {
    }

    const utf8* StringTable::findString( const hashed_string& key ) const
    {
        return findByHash( key.getHash() );
    }

    const utf8* StringTable::findStringByText( string_view keyText ) const
    {
        if ( keyText.empty() )
            return nullptr;
        return findByHash( hashed_string::computeHash( keyText ) );
    }

    const utf8* StringTable::findByHash( uint64 keyHash ) const
    {
        // 가리키는 문자열은 `LocalizedTextArena` 에 있어 테이블이 바뀌어도 사라지지 않는다. 락을 놓은 뒤 가지고 있어도 된다.
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     iter = _mapTable.find( keyHash );
        if ( iter != _mapTable.end() )
            return iter->second;
        return nullptr;
    }

    bool StringTable::contains( const hashed_string& key ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _mapTable.find( key.getHash() ) != _mapTable.end();
    }

    void StringTable::setString( const hashed_string& key, const string& value )
    {
        setStringByHash( key.getHash(), value );
    }

    void StringTable::setStringByHash( uint64 keyHash, string_view value )
    {
        const utf8* const        pStored = _arena.store( value );
        UniqueLock<shared_mutex> lock{ _mutex };
        _mapTable[keyHash] = pStored;
    }

    void StringTable::clear()
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        _mapTable.clear();
    }

    size_t StringTable::size() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _mapTable.size();
    }

    bool StringTable::empty() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _mapTable.empty();
    }
} // namespace sw
