#include "pch.h"

#include "Engine/Localization/StringTable.h"

#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/MemoryBlock.h"

namespace sw
{
    namespace
    {
        /**
         * @struct LocalizedTextArena
         * @brief 번역 문자열의 저장소입니다. **추가만 하고 엔진 수명이 끝날 때(`s_localizedTextArena` 의 범위가 닫힐 때)까지 해제하지 않습니다.** 같은 내용은 한 번만 둡니다.
         * @details `StringTable` 과 `LocalizationManager` 의 조회는 `const utf8*` 를 돌려주고 UI 와 워커가 그것을 가지고 있다. 테이블이 문자열을
         *          값으로 가지면 락을 놓은 뒤 `setString`, 다시 읽기, 언어 언로드가 그 저장소를 바꿀 때 가지고 있던 포인터가 해제된 메모리를
         *          가리킨다(조밀 해시 테이블이라 **다른 키를 넣기만 해도** 짧은 문자열이 옮겨진다). 언리얼 FText 가 공유 문자열을 가지고
         *          있는 것과 같은 보장을, 여기서는 문자열이 사라지지 않게 해서 준다. 같은 파일을 다시 읽으면 내용이 같으니 늘지 않는다.
         */
        struct LocalizedTextArena
        {
            static constexpr size_t kBlockBytes = 64 * 1024;

            /** @brief @p text 와 같은 내용의 안정된 C 문자열을 돌려줍니다(없으면 복사해 둡니다). */
            const utf8* store( string_view text )
            {
                const uint64            contentHash = HashUtil::computeStringHash64( text );
                std::scoped_lock<mutex> lock{ _mutex };
                const auto              it = _mapByContent.find( contentHash );
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

        private:
            mutex                              _mutex;
            vector<MemoryBlock>                _listBlock;
            unordered_map<uint64, const utf8*> _mapByContent;
            utf8*                              _pCursor{ nullptr };
            size_t                             _remainingBytes{ 0 };
        };

        /** @brief 모든 테이블이 함께 쓰는 저장소입니다. 엔진 수명 범위가 닫히면(엔진 종료) 소멸하고, 그 뒤로 앞서 돌려준 문자열 포인터는 모두 무효입니다. */
        ScopeOwned<LocalizedTextArena> s_localizedTextArena;

        /** @brief @p text 를 저장소에 둡니다. 엔진 수명이 끝난 뒤면 nullptr 입니다(테이블을 바꾸지 않는다). */
        const utf8* store( string_view text )
        {
            LocalizedTextArena* const pArena = s_localizedTextArena.getOrCreate();
            return pArena != nullptr ? pArena->store( text ) : nullptr;
        }
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* StringTable::getString( const hashed_string& key ) const
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
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          iter = _mapTable.find( keyHash );
        if ( iter != _mapTable.end() )
            return iter->second;
        return nullptr;
    }

    const utf8* StringTable::getString( const hashed_string& key, const utf8* pDefaultText ) const
    {
        const utf8* pFound = getString( key );
        return pFound != nullptr ? pFound : pDefaultText;
    }

    bool StringTable::contains( const hashed_string& key ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _mapTable.find( key.getHash() ) != _mapTable.end();
    }

    void StringTable::setString( const hashed_string& key, const string& value )
    {
        setStringByHash( key.getHash(), value );
    }

    void StringTable::setStringByHash( uint64 keyHash, string_view value )
    {
        const utf8* const pStored = store( value );
        if ( pStored == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapTable[keyHash] = pStored;
    }

    void StringTable::clear()
    {
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapTable.clear();
    }

    size_t StringTable::size() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _mapTable.size();
    }

    bool StringTable::empty() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _mapTable.empty();
    }
} // namespace sw
