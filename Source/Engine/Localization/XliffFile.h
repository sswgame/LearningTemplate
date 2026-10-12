/**
 * @file XliffFile.h
 * @brief XLIFF 1.2 파일을 정의합니다. 번역 관리 도구(Crowdin, memoQ, Phrase, Trados)와 주고받는 PO 의 다른 길입니다.
 * @details PO 와 같은 항목(`PortableObjectEntry`)을 읽고 쓴다 — 키는 `_context`, 원문은 `_source`, 번역은 `_translation` 이므로 수집 · 가져오기 코드가 두 형식을 같이 다룬다.
 *          한 항목은 다음과 같다.
 * @code
 *   <trans-unit id="Menu.Start" resname="Menu.Start">          ← 키
 *     <source>Start</source>                                   ← 지금 원문
 *     <target state="translated">시작</target>                  ← 번역(검토 표시면 state="needs-review-translation", 번역이 없으면 target 없음)
 *     <note from="developer">메뉴 첫 줄</note>                  ← 개발자 설명(_listExtractedComment)
 *     <note from="reference">Source/Game/Menu.cpp</note>       ← 원문 표의 origins(_listReference)
 *     <note from="previous-source">옛 원문</note>               ← 낡은 번역이 번역했던 원문
 *     <note from="translator">번역가 메모</note>                ← _translatorComment
 *   </trans-unit>
 * @endcode
 *          머리(`<file>`)에 `source-language`, `target-language`, `product-name`(프로젝트 이름)과 `X-Message-Format: ICU` 를 뜻하는 `datatype="x-icu-message"` 를 적는다.
 *          본문 속 인라인 태그(`<g>`, `<x/>`)는 지원하지 않는다 — ICU 자리표시자(`{name}`)가 글 그대로 들어가므로 필요 없다. 글의 앞뒤 공백은 읽을 때 다듬어진다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Localization/PortableObjectFile.h"

namespace sw
{
    /**
     * @class XliffFile
     * @brief XLIFF 1.2 파일 하나를 읽고 씁니다(UTF-8, `<file>` 하나).
     */
    class SW_API XliffFile
    {
    public:
        static constexpr const utf8* kExtension  = ".xlf";
        static constexpr const utf8* kFolderName = "xliff";

        /** @brief XLIFF 글을 읽습니다. 틀린 글이면 @p pOutError 에 이유를 적고 false 입니다. 버전이 1.2 가 아니면 거절합니다. */
        [[nodiscard]] bool parse( string_view text, string* pOutError = nullptr );
        /** @brief XLIFF 글을 만듭니다(항목 순서 그대로). */
        string toText() const;

        string                      _sourceLanguage;
        string                      _language; ///< 번역 문화권(`target-language`)
        string                      _projectName;
        vector<PortableObjectEntry> _listEntry;
    };
} // namespace sw
