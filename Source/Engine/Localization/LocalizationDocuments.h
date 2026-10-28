/**
 * @file LocalizationDocuments.h
 * @brief 로컬라이제이션 저작 파일 세트를 정의합니다. 원문 문자열 테이블(`*.strings.xml`), 문화권 번역 테이블(`<culture>.translation.xml`), 프로젝트(`*.locproject.xml`)입니다.
 * @details 원문 테이블이 원본이다(언리얼 String Table, 유니티 Shared Table Data). 키마다 원문, 맥락, 설명, 최대 길이, 나온 위치를 가진다.
 *          번역 테이블은 번역마다 **번역할 때의 원문 해시**를 가지며, 원문이 바뀌면 그 번역은 낡은(stale) 것이 되어 화면에 나오지 않는다(언리얼과 같다.
 *          낡은 번역 대신 다음 문화권으로 넘어간다). 모르는 속성 · 자식은 로드 오류이고, 쓰기는 키 사전 순이라 diff 가 작다. 형식은 XML 이다(키는 `<entry key="…">` 의 속성 — 키가 값마다 다른 맵이라 칸 이름으로 쓸 수 없다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct LocalizationTextUtil
     * @brief 원문 해시와 키 규칙처럼 저작 파일 세트가 함께 쓰는 유틸리티입니다.
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
        string         _context;        ///< 같은 글자의 다른 뜻을 구분하는 맥락입니다(번역가에게 보입니다. PO `#.`).
        string         _comment;        ///< 개발자 설명(PO `#.`)
        vector<string> _listOrigin;     ///< 수집기가 찾은 위치입니다(저장소 상대 파일, 리소스 경로). 비어 있으면 손으로 넣은 줄이고 수집기가 지우지 않습니다.
        uint32         _maxLength{ 0 }; ///< 번역의 최대 글자 수(UTF-8 글자, 0 = 제한 없음)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SourceStringTable
     * @brief 원문 문자열 테이블 파일 하나(`*.strings.xml`)입니다.
     * @details `<SourceStringTable culture="en"><entries><entry key="Menu.Start" source="Start" context="…" comment="…" maxLength="12"><origins><item>…</item></origins></entry></entries></SourceStringTable>`
     */
    class SW_API SourceStringTable
    {
    public:
        static constexpr const utf8* kExtension = ".strings.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `LocalizationDocuments.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "SourceStringTable", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );
        string             toXMLText() const;
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
        string _translatorComment; ///< 번역가 메모입니다(PO `# `). 왕복에서 유지합니다.
        uint64 _sourceHash{ 0 };   ///< 번역할 때의 원문 해시입니다. 0 이면 확인하지 않습니다(손으로 넣은 테이블).
        bool   _bReview{ false };  ///< 검토가 필요한지 나타냅니다(번역 메모리의 근사 일치, PO `#, fuzzy`). 화면에 나오지 않습니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 번역 한 줄이 지금 원문에 대해 어떤 상태인지입니다. */
    enum class TranslationState : uint8
    {
        Missing = 0, ///< 번역이 없습니다.
        Current,     ///< 지금 원문의 번역이며 화면에 나옵니다.
        Stale,       ///< 원문이 바뀐 뒤의 번역입니다.
        Review,      ///< 검토 표시가 있습니다.
        Orphan       ///< 원문 테이블에 없는 키의 번역입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class TranslationTable
     * @brief 문화권 하나의 번역 테이블 파일(`<culture>.translation.xml`)입니다.
     * @details `<TranslationTable culture="ko"><entries><entry key="Menu.Start" text="시작" sourceHash="…16 진수…" review="true" translatorComment="…" /></entries></TranslationTable>`
     */
    class SW_API TranslationTable
    {
    public:
        static constexpr const utf8* kExtension = ".translation.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `LocalizationDocuments.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "TranslationTable", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );
        string             toXMLText() const;
        [[nodiscard]] bool saveToFile( string_view absolutePath ) const;

        const TranslationEntry* findEntry( string_view key ) const;
        TranslationEntry&       getOrAddEntry( string_view key );
        [[nodiscard]] bool      removeEntry( string_view key );

        /** @brief @p pSource 원문에 대한 @p key 번역의 상태입니다(@p pSource 가 nullptr 이면 원문 테이블에 없는 키입니다). */
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
     * @brief 리플렉션으로 읽지 않는 데이터(손으로 읽는 XML 카탈로그, 스키마)에서 텍스트를 모으는 규칙 한 줄입니다(프로젝트의 `assetRules`).
     * @details `<rule files="items.xml" attribute="name" kind="text" context="Item name"><elements><item>Item</item></elements></rule>` 형태이며, 파일 이름이 `files` 로
     *          끝나는 XML 에서 그 요소들의 속성 값을 모은다. `kind` 는 `text`(키이거나 텍스트 그대로)와 `key`(키 참조이며 테이블에 있어야 한다)가 있다.
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
     * @brief 로컬라이제이션 프로젝트 파일(`*.locproject.xml`)입니다. 원문 문화권, 대상 문화권, 수집 설정을 담습니다(언리얼 Localization Dashboard 의 타깃 하나에 해당).
     * @details `<LocalizationProject name="engine" sourceCulture="en"><cultures><item>ko</item><item>ja</item></cultures><stringTables><item>engine.strings.xml</item></stringTables>
     *            <codeRoots><item>Source/Engine</item></codeRoots><assetRoots><item>engine/settings</item></assetRoots></LocalizationProject>` 형태이며, 테이블 이름은 프로젝트 파일 옆의 상대 경로, `codeRoots` 는 저장소 상대 경로,
     *          `assetRoots` 는 리소스 경로다. 수집기는 첫 테이블(`stringTables[0]`)에 쓴다. 번역 테이블은 문화권마다 `<culture>.translation.xml`,
     *          번역 메모리는 `tm/<culture>.tm.xml`, 교환 파일은 `po/<culture>.po` 이다. 파일을 훑지 않고 이름으로 읽으므로 팩 안에서도 같다.
     *          `stagedCultures` 는 출시하는 문화권이다(언리얼 패키징 설정의 Cultures to Stage). `cultures` 의 부분 집합이어야 하고, `--check-text` 는 이 문화권의
     *          빠지거나 낡은 번역을 실패로 본다. 목록 밖 문화권은 번역 중으로 보고 개수만 보고한다.
     */
    class SW_API LocalizationProject
    {
    public:
        static constexpr const utf8* kExtension = ".locproject.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `LocalizationDocuments.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "LocalizationProject", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError = nullptr );
        [[nodiscard]] bool loadFromFile( string_view absolutePath, string* pOutError = nullptr );

        /** @brief 프로젝트 파일 옆 @p fileName 의 경로입니다. */
        static string makeSiblingPath( string_view projectPath, string_view fileName );
        /** @brief 문화권 번역 테이블 경로(`<폴더>/<culture>.translation.xml`)입니다. */
        static string makeTranslationPath( string_view projectPath, string_view culture );
        /** @brief @p culture 가 출시 문화권(`stagedCultures`)이면 true 입니다. */
        bool isStagedCulture( string_view culture ) const;
        /**
         * @brief @p folder 아래 프로젝트마다 그 폴더의 번역 테이블 중 출시 문화권 밖의 것을 @p outListAbsPath 에 더합니다(`FileUtil::normalizePath` 한 절대 경로).
         * @details 팩 쿠킹이 담지 않는다(언리얼 Cultures to Stage — `PackCookParams::_listExcludedSourceFile`).
         * @return 프로젝트를 읽지 못하면 false 이고 이유는 @p outError 입니다 — 어느 테이블이 출시 문화권인지 모른 채 팩을 베이크하지 않는다.
         */
        [[nodiscard]] static bool collectUnstagedTranslationTables( string_view folder, vector<string>& outListAbsPath, string& outError );

        string                        _name;
        string                        _sourceCulture;
        vector<string>                _listCulture;
        vector<string>                _listStagedCulture;
        vector<string>                _listStringTable;
        vector<string>                _listCodeRoot;
        vector<string>                _listAssetRoot;
        vector<LocalizationAssetRule> _listAssetRule;
    };
} // namespace sw
