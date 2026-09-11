#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 로컬라이제이션 텍스트 파일의 형식. 경로 확장자로 정한다. */
    enum class StringTableTextFormat : uint8
    {
        Json = 0, ///< 확장자를 모르면 여기로 온다
        Xml,
        KeyValue ///< `.ini` · `.kv`
    };

    class SW_API StringTable
    {
    public:
        /**
         * @brief 경로 확장자로 텍스트 형식을 정합니다. **형식↔확장자 대응은 여기 하나다.**
         * @details 예전에는 `StringTable` 과 `LocalizationManager` 가 각자 `{ ".ini", ".kv" }` 를 들고
         *          같은 분기를 두 번 적었다. 목록이 둘이면 한쪽만 늘어난다.
         */
        static StringTableTextFormat detectTextFormat( string_view path );
        /** @brief 텍스트 형식으로 읽을 수 있는 확장자 목록입니다. 디렉터리를 훑을 때 이 순서로 시도한다. */
        static const vector<string_view>& getTextExtensions();

        bool        loadFromFile( const string& filePath );
        bool        loadFromJsonText( string_view jsonText );
        bool        loadFromXmlText( string_view xmlText );
        bool        loadFromKeyValueText( string_view kvText );
        bool        loadFromResource( string_view assetRelativePath );
        bool        saveToBinaryFile( string_view filePath ) const;
        bool        loadFromBinaryFile( string_view filePath );
        bool        saveToBinaryBuffer( vector<uint8>& outBytes ) const;
        bool        loadFromBinaryBuffer( const uint8* pData, size_t size );
        const utf8* getString( const hashed_string& key ) const;
        const utf8* getString( const hashed_string& key, const utf8* pDefaultText ) const;
        bool        contains( const hashed_string& key ) const;
        void        setString( const hashed_string& key, const string& value );
        void        clear();
        size_t      size() const;
        bool        empty() const;

    private:
        mutable std::shared_mutex     _mutex;
        unordered_map<uint64, string> _mapTable;
    };
} // namespace sw
