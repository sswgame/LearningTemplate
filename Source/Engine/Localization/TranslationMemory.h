/**
 * @file TranslationMemory.h
 * @brief 번역 메모리를 정의합니다. 원문과 번역 쌍을 문화권마다 모아 두고(`tm/<culture>.tm.xml`), 새 원문이나 바뀐 원문에 같거나 비슷한 이전 번역을 미리 채웁니다.
 * @details 수집(`--gather-text`)과 가져오기(`--import-po`)가 지금 원문의 번역(Current)을 쌍으로 넣으므로, 원문이 바뀌거나 키가 사라진 뒤에도 이전 쌍이 남는다.
 *          - 정확히 같은 원문은 그대로 채우고 지금 원문의 번역으로 받는다.
 *          - 정규화(대소문자, 공백, 끝 문장부호)가 같거나 유사도가 `kFuzzyThreshold` 이상이면 채우되 검토 표시(review)를 붙인다(화면에 나오지 않으며, PO 의 `#, fuzzy` 에 해당한다).
 *          유사도는 정규화한 글자열의 편집 거리(레벤슈타인, UTF-8 글자 단위)로 측정한다. 상용 CAT 도구(memoQ, Trados)의 퍼지 일치와 같은 생각이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "Engine/Common/AssetTypeRegistry.h"

namespace sw
{
    /** @brief 번역 메모리에서 찾은 것입니다. */
    struct TranslationMemoryMatch
    {
        string  _text;          ///< 이전 번역입니다.
        string  _matchedSource; ///< 그 번역의 원문입니다.
        float32 _score{ 0.0f }; ///< 1 이면 정확히 같습니다.
        bool    _bExact{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TranslationMemory
     * @brief 문화권 하나의 원문 → 번역 쌍입니다. `<TranslationMemory culture="ko"><entries><entry source="…" text="…" /></entries></TranslationMemory>`
     */
    class SW_API TranslationMemory
    {
    public:
        static constexpr const utf8* kExtension = ".tm.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `TranslationMemory.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "TranslationMemory", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        static constexpr const utf8*  kFolderName     = "tm";
        static constexpr float32      kFuzzyThreshold = 0.75f;

        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );
        string             toXMLText() const;
        [[nodiscard]] bool saveToFile( string_view absolutePath ) const;

        /** @brief 쌍을 넣습니다(같은 원문이면 번역을 바꿉니다). 바뀌었으면 true 입니다. */
        bool addPair( string_view source, string_view text );
        /** @brief 가장 비슷한 쌍을 찾습니다. @p minScore 미만이면 false 입니다. */
        [[nodiscard]] bool findBestMatch( string_view source, TranslationMemoryMatch& outMatch, float32 minScore = kFuzzyThreshold ) const;
        /** @brief 번역이 @p text 인 쌍의 원문을 찾습니다(낡은 번역의 이전 원문이며, PO `#| msgid` 에 씁니다). 없으면 빈 값입니다. */
        string findSourceOfText( string_view text ) const;

        /** @brief 비교용으로 정규화합니다. ASCII 소문자로 바꾸고, 공백 묶음을 하나로 줄이고, 앞뒤 공백과 끝 문장부호(. ! ? : …)를 뗍니다. */
        static string normalizeSource( string_view source );
        /** @brief 0..1 유사도입니다. 1 - 편집 거리 / 긴 쪽 글자 수(UTF-8 글자 단위)로 계산합니다. */
        static float32 computeSimilarity( string_view lhs, string_view rhs );

        const string& getCulture() const { return _culture; }
        void          setCulture( string_view culture );
        size_t        getEntryCount() const { return _mapSourceToText.size(); }

    private:
        string              _culture;
        map<string, string> _mapSourceToText;
    };
} // namespace sw
