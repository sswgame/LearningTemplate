/**
 * @file EditorLocalizationSheet.h
 * @brief 로컬라이제이션 프로젝트를 펼친 표(원문 · 번역 줄)와 게임 데이터 XML 파일 항목 — 값(읽고 쓰는 동작은 `EditorDataTableCommands.h`).
 * @details 값만 둔다: 백그라운드 잡(`EditorBackgroundIO.h`)과 Data Table 패널 Model 이 표를 값으로 들 때 파일 IO 커맨드를 끌어오지 않게 가른다(MVVM View include 계약, `docs/agents/EditorUI.md`).
 *          `LocalizationRecord` 의 판정 함수 본문은 `EditorDataTableCommands.cpp` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Localization/LocalizationDocuments.h"

namespace sw::editor
{
    /** @brief 테이블의 한 줄입니다. 키 하나의 원문과 메타데이터, 문화권마다의 번역을 가지고 있습니다. */
    struct LocalizationRecord
    {
        string                   _key;
        string                   _source;
        string                   _context;
        string                   _comment;
        vector<string>           _listTranslation; ///< `LocalizationSheet::_listCulture` 와 같은 순서
        vector<TranslationState> _listState;       ///< 문화권마다의 지금 상태. 읽은 값에 이번 편집을 더한 것이다(`DataTableModel` 이 고친다). 저장은 `Current` 인 줄의 번역을 지금 원문의 것으로 적는다
        uint32                   _maxLength{ 0 };
        uint32                   _tableIndex{ 0 };    ///< 이 키가 있는 원문 테이블(`LocalizationSheet::_listTablePath`)
        bool                     _bModified{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /** @brief 문화권 @p cultureIndex 번역의 글자 수(바이트가 아니라 UTF-8 코드 포인트)입니다. */
        uint32 measureTranslation( size_t cultureIndex ) const;
        /** @brief 문화권 @p cultureIndex 번역이 최대 길이를 넘으면 true 입니다. 최대 길이가 0 이면 제한이 없습니다. */
        bool isTranslationTooLong( size_t cultureIndex ) const;
        /** @brief 지금 원문의 번역이 아닌 문화권(없음 · 낡음 · 검토)이 하나라도 있으면 true 입니다. */
        bool needsWork() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로젝트 하나를 테이블로 펼친 것입니다. */
    struct LocalizationSheet
    {
        string                     _projectPath;
        string                     _sourceCulture;
        vector<string>             _listCulture;
        vector<string>             _listTablePath;
        vector<LocalizationRecord> _listRecord;
        vector<string>             _listRemovedKey; ///< 테이블에서 지운 키. 저장할 때 원문 테이블과 번역 테이블에서 지운다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 게임 데이터 XML 파일 항목 */
    struct GameDataFileEntry
    {
        string _fileName;
        string _absolutePath;
    };
} // namespace sw::editor
