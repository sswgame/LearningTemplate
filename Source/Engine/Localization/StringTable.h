#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class SW_API StringTable
    {
    public:
        /** @brief 언어 파일의 확장자입니다. 언어 파일은 JSON 객체(`{ "키": "문자열" }`) 하나뿐입니다. */
        static constexpr const utf8* kFileExtension = ".json";

        /** @brief @p path 가 언어 파일(`kFileExtension`)이면 true 입니다. 다른 확장자는 읽지 않습니다. */
        static bool isLanguageFile( string_view path );

        /** @brief 절대 · 리소스 경로의 언어 파일을 읽습니다. 언어 파일이 아니면 경고를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromFile( const string& filePath );
        [[nodiscard]] bool loadFromJsonText( string_view jsonText );
        /** @brief 리소스 상대 경로의 언어 파일을 읽습니다. 언어 파일이 아니면 경고를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view assetRelativePath );
        const utf8*        getString( const hashed_string& key ) const;
        const utf8*        getString( const hashed_string& key, const utf8* pDefaultText ) const;
        /**
         * @brief 키를 **intern 하지 않고** 조회합니다. 없으면 nullptr 입니다.
         * @details 표는 해시로만 열리므로 조회에 intern 이 필요 없습니다. 키가 아닐 수도 있는 텍스트로
         *          물어보는 자리(대사 원문 등)는 반드시 이쪽을 씁니다. `hashed_string` 을 만들면
         *          그 텍스트가 intern 아레나에 **영구히** 남습니다.
         */
        const utf8* findStringByText( string_view keyText ) const;
        /**
         * @brief 미리 구한 해시로 곧바로 조회합니다. 없으면 nullptr 입니다.
         * @details `LocalizationManager` 가 활성 언어와 폴백 언어를 훑을 때 **해시를 한 번만**
         *          구하려고 이것을 씁니다. 두 테이블에 같은 문자열을 두 번 해싱할 이유가 없습니다.
         */
        const utf8* findByHash( uint64 keyHash ) const;
        bool        contains( const hashed_string& key ) const;
        void        setString( const hashed_string& key, const string& value );
        void        clear();
        size_t      size() const;
        bool        empty() const;

    private:
        mutable std::shared_mutex _mutex;
        /// @brief 키 해시 → 번역 문자열. 문자열은 추가 전용 저장소(StringTable.cpp 의 `LocalizedTextArena`)에 있어 표가 바뀌어도 사라지지 않는다.
        unordered_map<uint64, const utf8*> _mapTable;
    };
} // namespace sw
