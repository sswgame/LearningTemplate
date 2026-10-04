/**
 * @file TextGatherer.h
 * @brief 글 수집기 — 코드(`SW_LOCTEXT` · `SW_LOCFORMAT`) · 리플렉션 데이터(`Meta = "Localizable"` 프로퍼티) · 다른 추출기가 넣은 글을 모아 원문 표에 합칩니다.
 * @details 언리얼 GatherText(소스 · 에셋 수집 → 매니페스트)와 같은 자리입니다. 합치기의 규칙:
 *          - 코드의 글은 키가 있다(`Namespace.Key`). 표에 없으면 더하고(added), 원문이 다르면 바꾼다(changed).
 *          - 데이터의 글은 **키이거나 글 그대로**다(대화 · 씬의 `textOrKey`). 어느 표에 그 키가 있으면 참조이고, 없으면 글 자체를 키로 더한다(gettext 와 같다 —
 *            글이 바뀌면 새 키가 되고, 옛 번역은 번역 메모리가 근사 일치로 넘겨준다).
 *          - 키 참조(설정 스키마의 `text=`)는 어느 표에 있어야 한다 — 없으면 오류다(원문을 지어낼 수 없다).
 *          - 수집기가 넣었던 줄(`origins` 가 있는 줄)이 이번에 나오지 않으면 지운다(removed). 손으로 넣은 줄(`origins` 없음)은 두고, 참조되면 자리를 단다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class SourceStringTable;

    /** @brief 모은 글이 어디서 왔는지 — 합치는 규칙이 다릅니다. */
    enum class GatheredTextKind : uint8
    {
        Keyed = 0,   ///< 코드 — 키와 원문이 함께 있다
        TextOrKey,   ///< 데이터 — 키이거나 글 그대로다
        KeyReference ///< 데이터 — 키만 가리킨다
    };
} // namespace sw

namespace sw
{
    /** @brief 모은 글 하나입니다(같은 키는 하나로 합쳐 자리만 늘어난다). */
    struct GatheredText
    {
        string           _key;
        string           _source;
        string           _context;
        vector<string>   _listOrigin;
        uint32           _maxLength{ 0 };
        GatheredTextKind _kind{ GatheredTextKind::Keyed };
    };
} // namespace sw

namespace sw
{
    /** @brief 수집 · 합치기에서 나온 문제입니다. */
    struct TextGatherIssue
    {
        string _location; ///< `파일:줄` 또는 리소스 경로
        string _message;
        bool   _bError{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 합치기 결과 — 무엇이 더해지고 바뀌고 지워졌는지입니다. */
    struct SW_API TextGatherReport
    {
        vector<string>          _listAdded;
        vector<string>          _listChanged;
        vector<string>          _listRemoved;
        vector<TextGatherIssue> _listIssue;
        uint32                  _unchangedCount{ 0 };

        /** @brief 표가 바뀌었는지(자리만 바뀐 것은 세지 않는다)입니다. */
        bool hasTextChanges() const { return _listAdded.empty() == false || _listChanged.empty() == false || _listRemoved.empty() == false; }
        /** @brief 오류가 있는지입니다. */
        bool hasErrors() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextGatherer
     * @brief 글을 모으고 원문 표에 합칩니다. 파일 시스템을 훑는 일은 부르는 쪽(`LocalizationTools`)이 하고, 여기는 글 · 파일 하나를 받습니다.
     */
    class SW_API TextGatherer
    {
    public:
        /** @brief 코드에서 이 이름의 매크로 호출을 모읍니다(세 인자 모두 문자열 리터럴). */
        static constexpr const utf8* kArrCodeMacro[] = { "SW_LOCTEXT", "SW_LOCFORMAT" };
        /** @brief 리플렉션 프로퍼티의 커스텀 메타 — 번역할 글(`Localizable`) · 사람이 읽는 글이 아님(`NotLocalizable`) · 최대 길이(`MaxLength=N`). */
        static constexpr const utf8* kMetaLocalizable    = "Localizable";
        static constexpr const utf8* kMetaNotLocalizable = "NotLocalizable";
        static constexpr const utf8* kMetaMaxLength      = "MaxLength";

        TextGatherer();

        /** @brief C++ 소스 글 하나에서 `SW_LOCTEXT` · `SW_LOCFORMAT` 을 모읍니다. 주석 · 문자열 · `#define` 줄 안의 것은 건너뜁니다. */
        void gatherCodeText( string_view sourceText, string_view originName );
        /**
         * @brief 리플렉션 XML(씬 · 프리팹 · 리플렉션으로 읽는 카탈로그) 하나를 훑습니다 — 원소 이름이 리플렉션 타입이면 `Localizable` 프로퍼티의 값을 모읍니다.
         * @details `Localizable` 도 `NotLocalizable` 도 아닌 문자열 프로퍼티에 사람이 읽는 글로 보이는 값(낱말 둘 이상 · 경로 아님)이 있으면 하드코딩 의심으로 경고합니다.
         */
        void gatherReflectedXml( string_view xmlText, string_view originName );

        void addKeyedText( string_view key, string_view source, string_view context, string_view origin, uint32 maxLength = 0 );
        void addTextOrKey( string_view textOrKey, string_view context, string_view origin, uint32 maxLength = 0 );
        void addKeyReference( string_view key, string_view origin );
        void addIssue( string_view location, string_view message, bool bError );
        /** @brief 훑은 파일 수를 셉니다(보고용). */
        void countFile() { ++_fileCount; }

        /**
         * @brief 모은 글을 수집 대상 표 @p inoutGatherTable 에 합칩니다. @p listOtherTable 은 같은 프로젝트의 다른(손으로 쓴) 원문 표입니다 — 키를 찾기만 합니다.
         */
        TextGatherReport mergeInto( SourceStringTable& inoutGatherTable, const vector<const SourceStringTable*>& listOtherTable ) const;

        const vector<GatheredText>&    getTexts() const { return _listText; }
        const vector<TextGatherIssue>& getIssues() const { return _listIssue; }
        uint32                         getFileCount() const { return _fileCount; }

    private:
        GatheredText& getOrAddText( string_view key, GatheredTextKind kind, string_view origin, bool& outAdded );

    private:
        vector<GatheredText>    _listText;
        map<string, size_t>     _mapTextIndex;
        vector<TextGatherIssue> _listIssue;
        uint32                  _fileCount;
    };
} // namespace sw
