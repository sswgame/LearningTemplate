#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @class StringTable
     * @brief 문화권 하나의 실행 테이블(키 해시 → 텍스트)입니다. 파일을 읽지 않으며, `LocalizationManager` 가 원문과 번역 테이블 문서에서 채웁니다.
     */
    class SW_API StringTable
    {
    public:
        const utf8* getString( const hashed_string& key ) const;
        const utf8* getString( const hashed_string& key, const utf8* pDefaultText ) const;
        /**
         * @brief 키를 **intern 하지 않고** 조회합니다. 없으면 nullptr 입니다.
         * @details 테이블은 해시로만 열리므로 조회에 intern 이 필요 없다. 키가 아닐 수도 있는 텍스트로
         *          물어보는 곳(대사 원문 등)은 반드시 이쪽을 쓴다. `hashed_string` 을 만들면
         *          그 텍스트가 intern 아레나에 **영구히** 남는다.
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
        /** @brief 키 해시로 넣습니다. 키를 intern 하지 않습니다(`hashed_string::computeHash` 와 같은 해시). */
        void   setStringByHash( uint64 keyHash, string_view value );
        void   clear();
        size_t size() const;
        bool   empty() const;

    private:
        mutable std::shared_mutex _mutex;
        /// @brief 키 해시 → 번역 문자열 테이블입니다. 문자열은 추가 전용 저장소(StringTable.cpp 의 `LocalizedTextArena`)에 있어 테이블이 바뀌어도 엔진 종료까지 사라지지 않습니다.
        unordered_map<uint64, const utf8*> _mapTable;
    };
} // namespace sw
