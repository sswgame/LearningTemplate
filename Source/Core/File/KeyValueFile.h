/**
 * @file KeyValueFile.h
 * @brief 여러 곳이 함께 쓰는 key=value 텍스트 테이블 파싱 · 로드입니다(세이브 슬롯, 에디터 ini).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include <optional>

namespace sw
{
    /**
     * @brief 투명 비교자 std::less<> (C++14/17) 를 씁니다. 임시 키 없이 find(string_view/const utf8*) 가 됩니다.
     * @note C++20 unordered_map 이종 조회는 C++17 빌드 때문에 쓰지 않습니다.
     */
    using KeyValueMap = map<string, string, std::less<>>;

    // ------------------------------------------------------------------------------
    // 1) KeyValueParseOptions — 주석/섹션 스킵
    // ------------------------------------------------------------------------------
    struct KeyValueParseOptions
    {
        utf8                   _commentChar;
        uint8                  _bSkipSemicolonComments : 1; ///< ';'로 시작하는 줄도 스킵
        uint8                  _bSkipBracketSections   : 1; ///< "[section]" 줄 스킵
        [[maybe_unused]] uint8 _reserved               : 6;

        /** @brief 세미콜론 주석과 대괄호 섹션을 건너뛰는 기본값입니다. */
        KeyValueParseOptions() noexcept
            : _commentChar{ '#' }
            , _bSkipSemicolonComments{ SW_TRUE }
            , _bSkipBracketSections{ SW_TRUE }
            , _reserved{ 0 } {}
    };
} // namespace sw

namespace sw
{
    /**
     * @class KeyValueFile
     * @brief `key=value` 줄을 파싱하고 경로에서 로드합니다
     * @details 리소스 상대 경로는 Core 가 모른다. 엔진은 `ResourceIO::readTextResource` 로 글을 읽어 `parse` 에 넘긴다.
     */
    class SW_API KeyValueFile
    {
    public:
        // ------------------------------------------------------------------------------
        // 2) 파싱 · 로드
        // ------------------------------------------------------------------------------
        /** @brief 텍스트를 outMap 에 파싱합니다(먼저 비웁니다). */
        [[nodiscard]] static bool parse( string_view text, KeyValueMap& outMap, KeyValueParseOptions opt = {} );

        /** @brief 절대 경로를 읽고 파싱합니다. */
        [[nodiscard]] static bool loadFile( string_view absPath, KeyValueMap& outMap, KeyValueParseOptions opt = {} );

        // ------------------------------------------------------------------------------
        // 3) 조회
        // ------------------------------------------------------------------------------
        // 폴백을 받던 `get` · `getInt` · `getFloat` · `getBool` 은 지웠다(데이터 주도 계획 D9). 없음은 아래 세 길로 형식이 가른다.
        /** @brief 키의 글을 그대로 찾습니다(다듬지 않는다). 없으면 nullptr 입니다. */
        [[nodiscard]] static const string* find( const KeyValueMap& mapData, string_view key );

        /**
         * @brief 반드시 있어야 하는 키 @p key 를 타입 T 로 읽습니다. 없거나 비었거나 깨지면 키와 함께 오류를 남기고 nullopt 입니다.
         * @details 폴백 인자를 받지 않는다(결정 기록 5-4). T 는 int32 · float32 · bool · string 입니다.
         */
        template <typename T>
        [[nodiscard]] static std::optional<T> readRequired( const KeyValueMap& mapData, string_view key )
        {
            T           value{};
            const uint8 read = parseValue( mapData, key, value );
            if ( read == kValueAbsent )
                reportMissing( key );
            return read == kValueRead ? std::optional<T>{ value } : std::nullopt;
        }
        /** @brief 없어도 되는 키를 읽습니다. 없으면(또는 빈 값) @p outValue 를 비우고 true, 깨졌으면 오류와 false 입니다. */
        template <typename T>
        [[nodiscard]] static bool readOptional( const KeyValueMap& mapData, string_view key, std::optional<T>& outValue )
        {
            T           value{};
            const uint8 read = parseValue( mapData, key, value );
            outValue         = read == kValueRead ? std::optional<T>{ value } : std::nullopt;
            return read != kValueInvalid;
        }
        /**
         * @brief 없어도 되는 키를 읽어 @p inoutValue 에 넣습니다. 없으면(또는 빈 값) 그대로 두고 true, 깨졌으면 오류와 false 입니다.
         * @details 기본값은 부르는 쪽이 넣어 둔 값(형식의 초기값)이고 여기서 정하지 않는다.
         */
        template <typename T>
        [[nodiscard]] static bool readOptionalInto( const KeyValueMap& mapData, string_view key, T& inoutValue )
        {
            T           value{};
            const uint8 read = parseValue( mapData, key, value );
            if ( read == kValueRead )
                inoutValue = value;
            return read != kValueInvalid;
        }
        // ------------------------------------------------------------------------------
        // 4) 쓰기
        // ------------------------------------------------------------------------------
        /**
         * @brief key=value 텍스트를 만듭니다.
         * @param headerComment 비어 있지 않으면 `# ` 을 앞에 붙여(이미 # 이면 그대로) 한 줄로 씁니다.
         * @param sectionName 비어 있지 않으면 `[sectionName]` 줄을 넣습니다.
         */
        static string dump( const KeyValueMap& mapData, string_view headerComment = {}, string_view sectionName = {} );
        /** @brief dump 결과를 절대 경로에 씁니다. */
        [[nodiscard]] static bool saveFile( string_view absPath, const KeyValueMap& mapData, string_view headerComment = {},
                                            string_view sectionName = {} );

        /**
         * @brief 비어 있지 않고 주석이 아닌 각 줄에 callback(trimmedLine)을 호출합니다.
         * @note 대괄호 섹션 헤더는 그대로 넘깁니다(처리는 callback 이 정합니다).
         */
        template <typename Fn>
        static void forEachContentLine( string_view text, Fn&& callback, utf8 commentChar = '#' )
        {
            StringUtil::forEachLine( text, [&]( string_view line )
            {
                line = StringUtil::trim( line ); // 뷰라 할당이 없다
                if ( line.empty() == false && line.front() != commentChar )
                    callback( line );
            } );
        }

    private:
        static constexpr uint8 kValueRead    = 0;
        static constexpr uint8 kValueAbsent  = 1;
        static constexpr uint8 kValueInvalid = 2;
        static uint8           parseValue( const KeyValueMap& mapData, string_view key, int32& outValue );
        static uint8           parseValue( const KeyValueMap& mapData, string_view key, float32& outValue );
        static uint8           parseValue( const KeyValueMap& mapData, string_view key, bool& outValue );
        static uint8           parseValue( const KeyValueMap& mapData, string_view key, string& outValue );
        static void            reportMissing( string_view key );
    };
} // namespace sw
