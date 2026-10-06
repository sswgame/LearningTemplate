/**
 * @file PortableObjectFile.h
 * @brief gettext PO 파일 — 번역가 · 번역 도구(Poedit · Crowdin · Weblate · memoQ)와 주고받는 형식입니다.
 * @details 언리얼 Localization Dashboard 의 Export/Import Text 와 같은 모양입니다. 한 항목:
 * @code
 *   # 번역가 메모                     ← 왕복에서 지킨다(TranslationEntry::_translatorComment)
 *   #. 개발자 설명 · 맥락 · 최대 길이     ← 원문 표의 comment · context · maxLength(읽기 전용)
 *   #: Source/Game/Menu.cpp           ← 원문 표의 origins
 *   #, fuzzy                          ← 검토 표시(review)
 *   #| msgid "옛 원문"                  ← 낡은 번역이 번역했던 원문(번역 메모리에서)
 *   msgctxt "Menu.Start"              ← 키
 *   msgid "Start"                     ← 지금 원문
 *   msgstr "시작"
 * @endcode
 *          머리 항목(msgid "")에 `Language` · `X-Localization-Project` · `X-Message-Format: ICU` 를 적습니다 — 복수형은 gettext 의 msgid_plural 이 아니라
 *          글 안의 ICU `{n, plural, …}` 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief PO 항목 하나입니다. */
    struct PortableObjectEntry
    {
        string         _context;              ///< msgctxt — 키
        string         _source;               ///< msgid
        string         _translation;          ///< msgstr
        string         _previousSource;       ///< `#| msgid`
        string         _translatorComment;    ///< `# ` 줄들(줄바꿈으로 잇는다)
        vector<string> _listExtractedComment; ///< `#.`
        vector<string> _listReference;        ///< `#:`
        bool           _bFuzzy{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PortableObjectFile
     * @brief PO 파일 하나를 읽고 씁니다(UTF-8, msgid_plural 없음).
     */
    class SW_API PortableObjectFile
    {
    public:
        static constexpr const utf8* kExtension  = ".po";
        static constexpr const utf8* kFolderName = "po";

        /** @brief PO 글을 읽습니다. 틀린 줄이면 @p pOutError 에 `줄: 이유` 를 적고 false 입니다. */
        [[nodiscard]] bool parse( string_view text, string* pOutError = nullptr );
        /** @brief PO 글을 만듭니다(머리 항목 포함). */
        string toText() const;

        /** @brief PO 따옴표 안에 넣을 글로 바꿉니다(백슬래시 · 따옴표 · 줄바꿈 · 탭을 이스케이프). */
        static string escapeText( string_view text );

        string                      _language;
        string                      _projectName;
        vector<PortableObjectEntry> _listEntry;
    };
} // namespace sw
