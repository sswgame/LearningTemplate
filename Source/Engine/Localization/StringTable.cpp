#include "pch.h"

#include "Engine/Localization/StringTable.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Format/KeyValueFile.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Xml/XmlDocument.h"

SW_LOG_CALLER( "StringTable" );
namespace sw
{
    namespace
    {
        /**
         * @struct LocalizedTextArena
         * @brief 번역 문자열의 저장소입니다. **추가만 하고 프로세스가 끝날 때까지 풀지 않습니다.** 같은 내용은 한 번만 둡니다.
         * @details `StringTable` · `LocalizationManager` 의 조회는 `const utf8*` 를 돌려주고 UI · 워커가 그것을 들고 있습니다. 예전에는
         *          표가 문자열을 값으로 가져, 락을 놓은 뒤 `setString` · 다시 읽기 · 언어 내리기가 그 저장소를 바꾸면 들고 있던 포인터가 해제된
         *          메모리를 가리켰습니다(조밀 해시 표라 **다른 키를 넣기만 해도** 짧은 문자열이 옮겨졌습니다). 언리얼 FText 가 공유 문자열을 들고
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

        constexpr uint32 kStringTableBinaryMagic   = 0x31425453; // 'STB1'
        constexpr uint32 kStringTableBinaryVersion = 1;

        struct StringTableInternal
        {
            [[nodiscard]] static bool loadTextByExtension( StringTable& table, string_view path, string_view text )
            {
                switch ( StringTable::detectTextFormat( path ) )
                {
                    case StringTableTextFormat::Xml:
                        return table.loadFromXmlText( text );
                    case StringTableTextFormat::KeyValue:
                        return table.loadFromKeyValueText( text );
                    case StringTableTextFormat::Json:
                    default:
                        return table.loadFromJsonText( text );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    StringTableTextFormat StringTable::detectTextFormat( string_view path )
    {
        if ( FileUtil::hasExtension( path, ".xml" ) )
            return StringTableTextFormat::Xml;
        if ( FileUtil::hasAnyExtension( path, { ".ini", ".kv" } ) )
            return StringTableTextFormat::KeyValue;
        return StringTableTextFormat::Json;
    }

    const vector<string_view>& StringTable::getTextExtensions()
    {
        static const vector<string_view> s_listExtension{ ".json", ".xml", ".ini", ".kv" };
        return s_listExtension;
    }

    bool StringTable::loadFromFile( const string& filePath )
    {
        if ( FileUtil::hasExtension( filePath, ".bin" ) )
            return loadFromBinaryFile( filePath );

        string text;
        if ( ResourceUtil::readTextResource( filePath, text ) == false && FileUtil::readTextFile( filePath, text ) == false )
        {
            SW_LOG_WARNING( "Failed to open StringTable file: %#", filePath.c_str() );
            return false;
        }

        bool bSuccess = StringTableInternal::loadTextByExtension( *this, filePath, text );

        if ( bSuccess )
            SW_LOG_INFO( "Loaded %# strings from %#.", _mapTable.size(), filePath.c_str() );

        return bSuccess;
    }

    bool StringTable::saveToBinaryBuffer( vector<uint8>& outBytes ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );

        outBytes.clear();
        outBytes.reserve( 16 + _mapTable.size() * 32 );

        auto appendBytes = [&]( const void* pSrc, size_t numBytes )
        {
            const uint8* pByteSrc = static_cast<const uint8*>( pSrc );
            outBytes.insert( outBytes.end(), pByteSrc, pByteSrc + numBytes );
        };

        const uint32 magic   = kStringTableBinaryMagic;
        const uint32 version = kStringTableBinaryVersion;
        const uint32 count   = static_cast<uint32>( _mapTable.size() );

        appendBytes( &magic, sizeof( magic ) );
        appendBytes( &version, sizeof( version ) );
        appendBytes( &count, sizeof( count ) );

        // **해시 순으로 적는다.** `_mapTable` 은 `unordered_map` 이라 순회 순서가 삽입 순서와
        // 할당 상황에 따라 달라진다. 그대로 적으면 **같은 내용을 두 번 구워도 파일 바이트가
        // 달라진다.** 미리 구워 두는 산출물에 그것은 diff · 캐시 · 검증을 모두 무의미하게 만든다.
        vector<uint64> listKeyHash;
        listKeyHash.reserve( _mapTable.size() );
        for ( const auto& [hash, pText] : _mapTable )
            listKeyHash.push_back( hash );
        std::sort( listKeyHash.begin(), listKeyHash.end() );

        for ( const uint64 keyHash : listKeyHash )
        {
            const utf8* const pText  = _mapTable.find( keyHash )->second;
            const uint32      strLen = static_cast<uint32>( StringUtil::strlen( pText ) );
            appendBytes( &keyHash, sizeof( keyHash ) );
            appendBytes( &strLen, sizeof( strLen ) );
            if ( strLen > 0 )
                appendBytes( pText, strLen );
        }

        return true;
    }

    bool StringTable::saveToBinaryFile( string_view filePath ) const
    {
        vector<uint8> buffer;
        if ( saveToBinaryBuffer( buffer ) == false )
            return false;
        return FileUtil::writeFile( filePath, buffer.data(), buffer.size() );
    }

    bool StringTable::loadFromBinaryBuffer( const uint8* pData, size_t size )
    {
        if ( pData == nullptr || size < 12 )
            return false;

        const uint8* pPtr = pData;
        const uint8* pEnd = pData + size;

        uint32 magic{ 0 };
        uint32 version{ 0 };
        uint32 count{ 0 };

        Memory::copy( &magic, pPtr, sizeof( magic ) );
        pPtr += sizeof( magic );
        Memory::copy( &version, pPtr, sizeof( version ) );
        pPtr += sizeof( version );
        Memory::copy( &count, pPtr, sizeof( count ) );
        pPtr += sizeof( count );

        if ( magic != kStringTableBinaryMagic || version != kStringTableBinaryVersion )
        {
            SW_LOG_WARNING( "Invalid StringTable binary format or version." );
            return false;
        }

        // **개수를 버퍼 크기로 먼저 자른다.** `count` 는 파일에서 온 값이라, 망가진 헤더가
        // 4,294,967,295 를 적어 두면 아래 `reserve` 가 그 자리에서 4G 개의 자리를 요구한다.
        // 항목을 하나도 읽어 보기 전에 죽는다(루프의 검사는 그 다음에야 돈다). 항목 하나는
        // 아무리 짧아도 키 해시(8) + 길이(4) = 12바이트이므로, **남은 바이트로 담을 수 있는
        // 최대 개수**가 정확한 상한이다. 임의로 고른 숫자가 아니다.
        constexpr size_t kMinEntryBytes = sizeof( uint64 ) + sizeof( uint32 );
        const size_t     remainingBytes = static_cast<size_t>( pEnd - pPtr );
        if ( static_cast<size_t>( count ) > remainingBytes / kMinEntryBytes )
        {
            SW_LOG_WARNING( "StringTable binary header claims %# entries but only %# bytes remain.", count, remainingBytes );
            return false;
        }

        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapTable.reserve( _mapTable.size() + count );

        for ( uint32 index = 0; index < count; ++index )
        {
            // **더하지 말고 뺀다.** 남은 바이트와 필요한 바이트를 비교한다. `pPtr + n` 은 버퍼 끝을
            // 한 칸 넘어서면 그 포인터를 만드는 것 자체가 규약 밖이다.
            if ( static_cast<size_t>( pEnd - pPtr ) < kMinEntryBytes )
            {
                SW_LOG_WARNING( "Corrupted StringTable binary buffer." );
                return false;
            }

            uint64 keyHash{ 0 };
            uint32 strLen{ 0 };
            Memory::copy( &keyHash, pPtr, sizeof( keyHash ) );
            pPtr += sizeof( keyHash );
            Memory::copy( &strLen, pPtr, sizeof( strLen ) );
            pPtr += sizeof( strLen );

            if ( static_cast<size_t>( pEnd - pPtr ) < static_cast<size_t>( strLen ) )
            {
                SW_LOG_WARNING( "Corrupted StringTable entry in binary buffer." );
                return false;
            }

            _mapTable[keyHash] = LocalizedTextArena::get().store( string_view{ reinterpret_cast<const utf8*>( pPtr ), strLen } );
            pPtr += strLen;
        }

        return true;
    }

    bool StringTable::loadFromBinaryFile( string_view filePath )
    {
        vector<uint8> buffer;
        if ( ( ResourceUtil::readBinaryResource( filePath, buffer ) == false && FileUtil::readFile( filePath, buffer ) == false ) || buffer.size() < 12 )
            return false;
        const bool bLoaded = loadFromBinaryBuffer( buffer.data(), buffer.size() );
        if ( bLoaded )
            SW_LOG_INFO( "Loaded %# strings from binary %#.", _mapTable.size(), string( filePath ).c_str() );
        return bLoaded;
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

    bool StringTable::loadFromXmlText( string_view xmlText )
    {
        xmlText = FileUtil::skipUtf8Bom( xmlText );

        XmlDocument doc;
        if ( doc.parse( xmlText ) == false )
        {
            SW_LOG_WARNING( "Failed to parse StringTable XML text." );
            return false;
        }

        XmlNode root = doc.getRoot( "GameStrings" );
        if ( root.isValid() == false )
            root = doc.getRoot( "Strings" );
        if ( root.isValid() == false )
            root = doc.getRoot();

        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "StringTable root node not found in XML text." );
            return false;
        }

        std::unique_lock<std::shared_mutex> lock( _mutex );
        for ( XmlNode strNode = root.findChild(); strNode; strNode = strNode.findNextSibling() )
        {
            const utf8* pKey = strNode.findAttribute( "key" );
            if ( StringUtil::isNullOrEmpty( pKey ) )
                pKey = strNode.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pKey ) )
                continue;

            const utf8*  pValue  = strNode.findAttribute( "value" );
            const uint64 keyHash = hashed_string::computeHash( pKey );
            if ( StringUtil::isNullOrEmpty( pValue ) == false )
                _mapTable[keyHash] = LocalizedTextArena::get().store( pValue );
            else
                _mapTable[keyHash] = LocalizedTextArena::get().store( strNode.getText() );
        }

        return true;
    }

    bool StringTable::loadFromKeyValueText( string_view kvText )
    {
        kvText = FileUtil::skipUtf8Bom( kvText );

        KeyValueMap map;
        if ( KeyValueFile::parse( kvText, map ) == false )
        {
            SW_LOG_WARNING( "Failed to parse StringTable KeyValue text." );
            return false;
        }

        std::unique_lock<std::shared_mutex> lock( _mutex );
        for ( const auto& pair : map )
        {
            _mapTable[hashed_string::computeHash( pair.first )] = LocalizedTextArena::get().store( pair.second );
        }

        return true;
    }

    bool StringTable::loadFromResource( string_view assetRelativePath )
    {
        if ( FileUtil::hasExtension( assetRelativePath, ".bin" ) )
        {
            vector<uint8> buffer;
            if ( ResourceUtil::readBinaryResource( assetRelativePath, buffer ) == false )
            {
                SW_LOG_WARNING( "Failed to read binary resource StringTable: %#", assetRelativePath );
                return false;
            }
            return loadFromBinaryBuffer( buffer.data(), buffer.size() );
        }

        string text;
        string absPath;
        if ( ResourceUtil::readTextResource( assetRelativePath, text, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read resource StringTable: %#", assetRelativePath );
            return false;
        }

        return StringTableInternal::loadTextByExtension( *this, assetRelativePath, text );
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
