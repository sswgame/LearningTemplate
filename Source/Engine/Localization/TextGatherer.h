/**
 * @file TextGatherer.h
 * @brief 텍스트 수집기를 정의합니다. 코드(`SW_LOCTEXT`, `SW_LOCFORMAT`), 리플렉션 데이터(`Meta = "Localizable"` 프로퍼티), 다른 추출기가 넣은 텍스트를 모아 원문 테이블에 합칩니다.
 * @details 언리얼 GatherText(소스와 에셋 수집 → 매니페스트)에 해당한다. 합치기 규칙은 다음과 같다.
 *          - 코드의 텍스트는 키가 있다(`Namespace.Key`). 테이블에 없으면 더하고(added), 원문이 다르면 바꾼다(changed).
 *          - 데이터의 텍스트는 **키이거나 텍스트 그대로**다(대화, 씬의 `textOrKey`). 어느 테이블에 그 키가 있으면 참조이고, 없으면 텍스트 자체를 키로 더한다(gettext 와 같다.
 *            텍스트가 바뀌면 새 키가 되고, 이전 번역은 번역 메모리가 근사 일치로 넘겨준다).
 *          - 키 참조(설정 스키마의 `text=`)는 어느 테이블에 있어야 한다. 없으면 오류다(원문을 지어낼 수 없다).
 *          - 수집기가 넣었던 줄(`origins` 가 있는 줄)이 이번에 나오지 않으면 지운다(removed). 손으로 넣은 줄(`origins` 없음)은 두고, 참조되면 위치를 단다.
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

    /** @brief 모은 텍스트가 어디서 왔는지 나타냅니다. 합치는 규칙이 다릅니다. */
    enum class GatheredTextKind : uint8
    {
        Keyed = 0,   ///< 코드이며 키와 원문이 함께 있습니다.
        TextOrKey,   ///< 데이터이며 키이거나 텍스트 그대로입니다.
        KeyReference ///< 데이터이며 키만 가리킵니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 모은 텍스트 하나입니다(같은 키는 하나로 합치고 위치만 늘어납니다). */
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
    /** @brief 수집과 합치기에서 나온 문제입니다. */
    struct TextGatherIssue
    {
        string _location; ///< `파일:줄` 또는 리소스 경로입니다.
        string _message;
        bool   _bError{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 합치기 결과입니다. 무엇이 더해지고 바뀌고 지워졌는지 나타냅니다. */
    struct SW_API TextGatherReport
    {
        vector<string>          _listAdded;
        vector<string>          _listChanged;
        vector<string>          _listRemoved;
        vector<TextGatherIssue> _listIssue;
        uint32                  _unchangedCount{ 0 };

        /** @brief 테이블이 바뀌었는지 반환합니다(위치만 바뀐 것은 세지 않습니다). */
        bool hasTextChanges() const { return _listAdded.empty() == false || _listChanged.empty() == false || _listRemoved.empty() == false; }
        /** @brief 오류가 있는지입니다. */
        bool hasErrors() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextGatherer
     * @brief 텍스트를 모으고 원문 테이블에 합칩니다. 파일 시스템을 훑는 일은 부르는 쪽(`LocalizationTools`)이 하고, 여기서는 텍스트와 파일 하나를 받습니다.
     */
    class SW_API TextGatherer
    {
    public:
        /** @brief 코드에서 이 이름의 매크로 호출을 모읍니다. 앞의 둘은 (네임스페이스, 키, 원문) 세 리터럴, 뒤의 둘은 (네임스페이스, 키) 두 리터럴이다. */
        static constexpr const utf8* kArrCodeMacro[] = { "SW_LOCTEXT", "SW_LOCFORMAT", "SW_LOCKEY", "SW_LOCKEYFORMAT" };
        /** @brief 리플렉션 프로퍼티의 커스텀 메타입니다. 번역할 텍스트(`Localizable`), 사람이 읽는 텍스트가 아님(`NotLocalizable`), 최대 길이(`MaxLength=N`)가 있습니다. */
        static constexpr const utf8* kMetaLocalizable    = "Localizable";
        static constexpr const utf8* kMetaNotLocalizable = "NotLocalizable";
        static constexpr const utf8* kMetaMaxLength      = "MaxLength";

        TextGatherer();

        /** @brief C++ 소스 텍스트 하나에서 `SW_LOCTEXT` 와 `SW_LOCFORMAT` 을 모읍니다. 주석, 문자열, `#define` 줄 안의 것은 건너뜁니다. */
        void gatherCodeText( string_view sourceText, string_view originName );
        /**
         * @brief 리플렉션 XML(씬, 프리팹, 리플렉션으로 읽는 카탈로그) 하나를 훑습니다. 요소 이름이 리플렉션 타입이면 `Localizable` 프로퍼티의 값을 모읍니다.
         * @details `Localizable` 도 `NotLocalizable` 도 아닌 문자열 프로퍼티에 사람이 읽는 텍스트로 보이는 값(단어 둘 이상이고 경로가 아님)이 있으면 하드코딩 의심으로 경고한다.
         */
        void gatherReflectedXML( string_view xmlText, string_view originName );

        void addKeyedText( string_view key, string_view source, string_view context, string_view origin, uint32 maxLength = 0 );
        void addTextOrKey( string_view textOrKey, string_view context, string_view origin, uint32 maxLength = 0 );
        void addKeyReference( string_view key, string_view origin );
        void addIssue( string_view location, string_view message, bool bError );
        /** @brief 훑은 파일 수를 셉니다(보고용). */
        void countFile() { ++_fileCount; }

        /**
         * @brief 모은 텍스트를 수집 대상 테이블 @p inoutGatherTable 에 합칩니다. @p listOtherTable 은 같은 프로젝트의 다른(손으로 쓴) 원문 테이블이며 키를 찾기만 합니다.
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
