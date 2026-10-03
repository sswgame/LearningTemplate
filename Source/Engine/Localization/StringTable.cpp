#include "pch.h"

#include "Engine/Localization/StringTable.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

SW_LOG_CALLER( "StringTable" );
namespace sw
{
    namespace
    {
        /**
         * @struct LocalizedTextArena
         * @brief 번역 문자열의 저장소입니다. **추가만 하고 프로세스가 끝날 때까지 풀지 않습니다.** 같은 내용은 한 번만 둡니다.
         * @details `StringTable` · `LocalizationManager` 의 조회는 `const utf8*` 를 돌려주고 UI · 워커가 그것을 들고 있습니다. 표가 문자열을
         *          값으로 가지면 락을 놓은 뒤 `setString` · 다시 읽기 · 언어 내리기가 그 저장소를 바꿀 때 들고 있던 포인터가 해제된 메모리를
         *          가리킵니다(조밀 해시 표라 **다른 키를 넣기만 해도** 짧은 문자열이 옮겨집니다). 언리얼 FText 가 공유 문자열을 들고
         *          있는 것과 같은 보장을, 여기서는 문자열이 사라지지 않게 해서 줍니다. 같은 파일을 다시 읽으면 내용이 같으니 늘지 않습니다.
         */
        struct LocalizedTextArena
        {
            static constexpr size_t kBlockBytes = 64 * 1024;

            /** @brief 프로세스에 하나입니다. */
            static LocalizedTextArena& get()
            {
                static LocalizedTextArena s_arena;
                return s_arena;
            }

            ~LocalizedTextArena()
            {
                for ( utf8* pBlock : _listBlock )
                    Memory::free( pBlock );
            }

            /** @brief @p text 와 같은 내용의 안정된 C 문자열을 돌려줍니다(없으면 복사해 둡니다). */
            const utf8* store( string_view text )
            {
                const uint64            contentHash = StringUtil::computeHash64( text );
                std::scoped_lock<mutex> lock{ _mutex };
                const auto              it = _mapByContent.find( contentHash );
                if ( it != _mapByContent.end() && string_view{ it->second } == text )
                    return it->second;

                const size_t bytes = text.size() + 1;
                if ( _pCursor == nullptr || bytes > _remainingBytes )
                {
                    const size_t blockBytes = bytes > kBlockBytes ? bytes : kBlockBytes;
                    _pCursor                = static_cast<utf8*>( Memory::allocate( blockBytes ) );
                    _remainingBytes         = blockBytes;
                    _listBlock.push_back( _pCursor );
                }
                utf8* const pStored = _pCursor;
                Memory::copy( pStored, text.data(), text.size() );
                pStored[text.size()] = '\0';
                _pCursor += bytes;
                _remainingBytes -= bytes;
                // 해시가 같은 다른 내용이면 먼저 둔 것을 남긴다(다음에 같은 내용이 오면 새로 둔다 — 드물다).
                if ( it == _mapByContent.end() )
                    _mapByContent.emplace( contentHash, pStored );
                return pStored;
            }

        private:
            mutex                              _mutex;
            vector<utf8*>                      _listBlock;
            unordered_map<uint64, const utf8*> _mapByContent;
            utf8*                              _pCursor{ nullptr };
            size_t                             _remainingBytes{ 0 };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool StringTable::isLanguageFile( string_view path )
    {
        return FileUtil::hasExtension( path, kFileExtension );
    }

    bool StringTable::loadFromFile( const string& filePath )
    {
        if ( isLanguageFile( filePath ) == false )
        {
            SW_LOG_WARNING( "Not a language file (expected %#): %#", kFileExtension, filePath.c_str() );
            return false;
        }

        string text;
        if ( ResourceUtil::readTextResource( filePath, text ) == false && FileUtil::readTextFile( filePath, text ) == false )
        {
            SW_LOG_WARNING( "Failed to open StringTable file: %#", filePath.c_str() );
            return false;
        }

        const bool bSuccess = loadFromJsonText( text );

        if ( bSuccess )
            SW_LOG_INFO( "Loaded %# strings from %#.", _mapTable.size(), filePath.c_str() );

        return bSuccess;
    }

    bool StringTable::loadFromJsonText( string_view jsonText )
    {
        jsonText = FileUtil::skipUtf8Bom( jsonText );

        JsonDocument doc;
        if ( doc.parse( jsonText ) == false )
        {
            SW_LOG_WARNING( "Failed to parse StringTable JSON text." );
            return false;
        }

        const JsonValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_WARNING( "StringTable root is not an object in JSON text." );
            return false;
        }

        std::unique_lock<std::shared_mutex> lock( _mutex );
        for ( const string& key : root.getMemberNames() )
        {
            const JsonValue value = root.get( key, false );
            if ( value.isValid() == false || value.isObject() || value.isArray() )
                continue;
            // 키는 intern 하지 않는다 — 해시만 같으면 된다(`computeHash` 는 `getHash` 와 같은 값). 파일의 키로 전역 이름 표를 채우지 않는다.
            _mapTable[hashed_string::computeHash( key )] = LocalizedTextArena::get().store( value.asString() );
        }

        return true;
    }

    bool StringTable::loadFromResource( string_view assetRelativePath )
    {
        if ( isLanguageFile( assetRelativePath ) == false )
        {
            SW_LOG_WARNING( "Not a language file (expected %#): %#", kFileExtension, assetRelativePath );
            return false;
        }

        string text;
        string absPath;
        if ( ResourceUtil::readTextResource( assetRelativePath, text, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read resource StringTable: %#", assetRelativePath );
            return false;
        }

        return loadFromJsonText( text );
    }

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
        // 가리키는 문자열은 `LocalizedTextArena` 에 있어 표가 바뀌어도 사라지지 않는다 — 락을 놓은 뒤 들고 있어도 된다.
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
        const utf8* const                   pStored = LocalizedTextArena::get().store( value );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapTable[key.getHash()] = pStored;
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
