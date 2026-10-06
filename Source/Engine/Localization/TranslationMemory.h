/**
 * @file TranslationMemory.h
 * @brief 번역 메모리 — 원문 → 번역 쌍을 문화권마다 모아 두고(`tm/<culture>.tm.json`), 새 · 바뀐 원문에 같거나 비슷한 옛 번역을 미리 채웁니다.
 * @details 수집(`--gather-text`) · 가져오기(`--import-po`)가 지금 원문의 번역(Current)을 쌍으로 넣으므로, 원문이 바뀌거나 키가 사라진 뒤에도 옛 쌍이 남습니다.
 *          - 정확히 같은 원문 → 그대로 채우고 지금 원문의 번역으로 받는다.
 *          - 정규화(대소문자 · 공백 · 끝 문장부호)가 같거나 유사도 `kFuzzyThreshold` 이상 → 채우되 검토 표시(review — 화면에 안 나온다, PO 의 `#, fuzzy`).
 *          유사도는 정규화한 글자열의 편집 거리(레벤슈타인, UTF-8 글자 단위)로 잽니다. 상용 CAT 도구(memoQ · Trados)의 퍼지 일치와 같은 생각입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @brief 번역 메모리에서 찾은 것입니다. */
    struct TranslationMemoryMatch
    {
        string  _text;          ///< 옛 번역
        string  _matchedSource; ///< 그 번역의 원문
        float32 _score{ 0.0f }; ///< 1 이면 정확히 같다
        bool    _bExact{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TranslationMemory
     * @brief 문화권 하나의 원문 → 번역 쌍입니다. `{ "culture": "ko", "entries": [ { "source": "…", "text": "…" } ] }`
     */
    class SW_API TranslationMemory
    {
    public:
        static constexpr const utf8* kExtension      = ".tm.json";
        static constexpr const utf8* kFolderName     = "tm";
        static constexpr float32     kFuzzyThreshold = 0.75f;

        [[nodiscard]] bool loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );
        string             toJsonText() const;
        [[nodiscard]] bool saveToFile( string_view absolutePath ) const;

        /** @brief 쌍을 넣습니다(같은 원문이면 번역을 바꿉니다). 바뀌었으면 true 입니다. */
        bool addPair( string_view source, string_view text );
        /** @brief 가장 비슷한 쌍을 찾습니다. @p minScore 미만이면 false 입니다. */
        [[nodiscard]] bool findBestMatch( string_view source, TranslationMemoryMatch& outMatch, float32 minScore = kFuzzyThreshold ) const;
        /** @brief 번역이 @p text 인 쌍의 원문을 찾습니다(낡은 번역의 옛 원문 — PO `#| msgid`). 없으면 빈 값입니다. */
        string findSourceOfText( string_view text ) const;

        /** @brief 비교용 정규화 — ASCII 소문자, 공백 묶음 하나로, 앞뒤 공백과 끝 문장부호(. ! ? : …)를 뗍니다. */
        static string normalizeSource( string_view source );
        /** @brief 0..1 유사도 — 1 - 편집 거리 / 긴 쪽 글자 수(UTF-8 글자 단위). */
        static float32 computeSimilarity( string_view lhs, string_view rhs );

        const string& getCulture() const { return _culture; }
        void          setCulture( string_view culture );
        size_t        getEntryCount() const { return _mapSourceToText.size(); }

    private:
        string              _culture;
        map<string, string> _mapSourceToText;
    };
} // namespace sw
