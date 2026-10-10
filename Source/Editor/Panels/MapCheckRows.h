/**
 * @file MapCheckRows.h
 * @brief 맵 검사 패널의 줄을 고르고 세는 판단입니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionValidation.h"

namespace sw::editor
{
    /** @brief 맵 검사 패널의 거르기입니다. */
    struct MapCheckFilter
    {
        string _search;                ///< 오브젝트 · 타입 · 프로퍼티 · 메시지에서 찾는 글(`EditorListFilter`)
        bool   _bShowErrors{ true };   ///< 오류를 보인다
        bool   _bShowWarnings{ true }; ///< 경고를 보인다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 무게별 결과 수입니다(거르기 전). */
    struct MapCheckCounts
    {
        uint32 _errorCount{ 0 };
        uint32 _warningCount{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct MapCheckRows
     * @brief `ValidationIssueLog` 의 결과로 패널 줄을 만듭니다. 오류가 먼저, 그 안에서 오브젝트 · 타입 · 프로퍼티 순입니다(언리얼 Map Check 와 같다).
     */
    struct MapCheckRows
    {
        /** @brief @p listIssue 를 @p filter 로 걸러 정렬한 줄을 @p outListRow 에 채우고(먼저 비운다) 거르기 전 무게별 수를 셉니다. */
        static void populate( const vector<ValidationIssue>& listIssue, const MapCheckFilter& filter, vector<ValidationIssue>& outListRow, MapCheckCounts& outCounts );
        /** @brief 무게별 수만 셉니다(상태줄). */
        static MapCheckCounts countIssues( const vector<ValidationIssue>& listIssue );
    };
} // namespace sw::editor
