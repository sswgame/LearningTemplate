/**
 * @file LocalizationDocuments.h
 * @brief 로컬라이제이션 저작 파일 셋 — 원문 문자열 표(`*.strings.json`) · 문화권 번역 표(`<culture>.translation.json`) · 프로젝트(`*.locproject.json`) — 입니다.
 * @details 원문 표가 정본입니다(언리얼 String Table · 유니티 Shared Table Data). 키마다 원문 · 맥락 · 설명 · 최대 길이 · 나온 자리를 듭니다.
 *          번역 표는 번역마다 **번역할 때의 원문 해시**를 들어, 원문이 바뀌면 그 번역은 낡은(stale) 것이 되고 화면에 나오지 않습니다(언리얼과 같다 —
 *          낡은 번역 대신 다음 문화권으로 떨어진다). 모르는 칸은 로드 오류이고, 쓰기는 키 사전 순이라 diff 가 작습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct LocalizationTextUtil
     * @brief 원문 해시 · 키 규칙처럼 저작 파일 셋이 함께 쓰는 것입니다.
     */
    struct SW_API LocalizationTextUtil
    {
        /** @brief 원문 해시입니다(대소문자 구분 64 비트 FNV-1a). 0 은 "해시 없음" 이라 쓰지 않습니다. */
        static uint64 computeSourceHash( string_view sourceText );
        /** @brief 16 자리 소문자 16 진수입니다. */
        static string formatSourceHash( uint64 sourceHash );
        /** @brief 16 진수를 읽습니다. 틀린 글이면 false 입니다. */
        [[nodiscard]] static bool tryParseSourceHash( string_view text, uint64& outSourceHash );
        /** @brief 코드의 키(`SW_LOCTEXT( "Namespace", "Key", … )`)를 표의 키(`Namespace.Key`)로 잇습니다. 이름공간이 비면 키 그대로입니다. */
        static string makeFullKey( string_view textNamespace, string_view key );
    };
} // namespace sw

namespace sw
{
    /** @brief 원문 표의 한 줄입니다. */
    struct SourceTextEntry
    {
        string         _source;         ///< 원문(ICU MessageFormat 패턴)
        string         _context;        ///< 같은 글자의 다른 뜻을 가르는 맥락(번역가에게 보인다 — PO `#.`)
        string         _comment;        ///< 개발자 설명(PO `#.`)
        vector<string> _listOrigin;     ///< 수집기가 찾은 자리(저장소 상대 파일 · 리소스 경로). 비어 있으면 손으로 넣은 줄이고 수집기가 지우지 않는다
        uint32         _maxLength{ 0 }; ///< 번역의 최대 글자 수(UTF-8 글자, 0 = 제한 없음)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SourceStringTable
     * @brief 원문 문자열 표 파일 하나(`*.strings.json`)입니다.
     * @details `{ "culture": "en", "entries": { "Menu.Start": { "source": "Start", "context": "…", "comment": "…", "maxLength": 12, "origins": [ … ] } } }`
     */
    class SW_API SourceStringTable
    {
    public:
        static constexpr const utf8* kExtension = ".strings.json";

        [[nodiscard]] bool loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );
        string             toJsonText() const;
        [[nodiscard]] bool saveToFile( string_view absolutePath ) const;

        const SourceTextEntry* findEntry( string_view key ) const;
        SourceTextEntry&       getOrAddEntry( string_view key );
        [[nodiscard]] bool     removeEntry( string_view key );

        const string&                       getCulture() const { return _culture; }
        void                                setCulture( string_view culture );
        const map<string, SourceTextEntry>& getEntries() const { return _mapEntry; }
        map<string, SourceTextEntry>&       getMutableEntries() { return _mapEntry; }

    private:
        string                       _culture;
        map<string, SourceTextEntry> _mapEntry;
    };
} // namespace sw

namespace sw
{
    /** @brief 번역 표의 한 줄입니다. */
    struct TranslationEntry
    {
        string _text;              ///< 번역(ICU MessageFormat 패턴)
        string _translatorComment; ///< 번역가 메모(PO `# `) — 왕복에서 지킨다
        uint64 _sourceHash{ 0 };   ///< 번역할 때의 원문 해시. 0 이면 확인하지 않는다(손으로 넣은 표)
        bool   _bReview{ false };  ///< 검토가 필요하다(번역 메모리의 근사 일치 · PO `#, fuzzy`) — 화면에 나오지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 번역 한 줄이 지금 원문에 대해 어떤 상태인지입니다. */
    enum class TranslationState : uint8
    {
        Missing = 0, ///< 번역이 없다
        Current,     ///< 지금 원문의 번역이다 — 화면에 나온다
        Stale,       ///< 원문이 바뀐 뒤의 번역이다
        Review,      ///< 검토 표시가 있다
        Orphan       ///< 원문 표에 없는 키의 번역이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class TranslationTable
     * @brief 문화권 하나의 번역 표 파일(`<culture>.translation.json`)입니다.
     * @details `{ "culture": "ko", "entries": { "Menu.Start": { "text": "시작", "sourceHash": "…16 진수…", "review": true, "translatorComment": "…" } } }`
     */
    class SW_API TranslationTable
    {
    public:
        static constexpr const utf8* kExtension = ".translation.json";

        [[nodiscard]] bool loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );
        string             toJsonText() const;
        [[nodiscard]] bool saveToFile( string_view absolutePath ) const;

        const TranslationEntry* findEntry( string_view key ) const;
        TranslationEntry&       getOrAddEntry( string_view key );
        [[nodiscard]] bool      removeEntry( string_view key );

        /** @brief @p pSource 원문에 대한 @p key 번역의 상태입니다(@p pSource 가 nullptr 이면 원문 표에 없는 키). */
        TranslationState computeState( string_view key, const SourceTextEntry* pSource ) const;

        const string&                        getCulture() const { return _culture; }
        void                                 setCulture( string_view culture );
        const map<string, TranslationEntry>& getEntries() const { return _mapEntry; }
        map<string, TranslationEntry>&       getMutableEntries() { return _mapEntry; }

    private:
        string                        _culture;
        map<string, TranslationEntry> _mapEntry;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LocalizationAssetRule
     * @brief 리플렉션으로 읽지 않는 데이터(손으로 읽는 XML 카탈로그 · 스키마)에서 글을 모으는 규칙 한 줄입니다(프로젝트의 `assetRules`).
     * @details `{ "files": "items.xml", "elements": [ "Item" ], "attribute": "name", "kind": "text", "context": "Item name" }` — 파일 이름이 `files` 로
     *          끝나는 XML 에서 그 원소들의 속성 값을 모읍니다. `kind` 는 `text`(키이거나 글 그대로) · `key`(키 참조 — 표에 있어야 한다).
     */
    struct LocalizationAssetRule
    {
        string         _fileSuffix;
        vector<string> _listElement;
        string         _attribute;
        string         _context;
        bool           _bKeyReference{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalizationProject
     * @brief 로컬라이제이션 프로젝트 파일(`*.locproject.json`) — 원문 문화권 · 대상 문화권 · 수집 설정입니다(언리얼 Localization Dashboard 의 타깃 하나).
     * @details `{ "name": "engine", "sourceCulture": "en", "cultures": [ "ko", "ja" ], "stringTables": [ "engine.strings.json" ],
     *            "codeRoots": [ "Source/Engine" ], "assetRoots": [ "engine/settings" ] }` — 표 이름은 프로젝트 파일 옆 상대, `codeRoots` 는 저장소 상대,
     *          `assetRoots` 는 리소스 경로입니다. 수집기는 첫 표(`stringTables[0]`)에 씁니다. 번역 표는 문화권마다 `<culture>.translation.json`,
     *          번역 메모리는 `tm/<culture>.tm.json`, 교환 파일은 `po/<culture>.po` 입니다. 파일을 훑지 않고 이름으로 읽어 팩 안에서도 같습니다.
     */
    class SW_API LocalizationProject
    {
    public:
        static constexpr const utf8* kExtension = ".locproject.json";

        [[nodiscard]] bool loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );

        /** @brief 프로젝트 파일 옆 @p fileName 의 경로입니다. */
        static string makeSiblingPath( string_view projectPath, string_view fileName );
        /** @brief 문화권 번역 표 경로(`<폴더>/<culture>.translation.json`)입니다. */
        static string makeTranslationPath( string_view projectPath, string_view culture );

        string                        _name;
        string                        _sourceCulture;
        vector<string>                _listCulture;
        vector<string>                _listStringTable;
        vector<string>                _listCodeRoot;
        vector<string>                _listAssetRoot;
        vector<LocalizationAssetRule> _listAssetRule;
    };
} // namespace sw
