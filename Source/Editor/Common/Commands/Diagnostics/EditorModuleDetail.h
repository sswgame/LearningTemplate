/**
 * @file EditorModuleDetail.h
 * @brief 모듈 창 상세 영역 한 모듈의 내용(값)입니다. `EditorModuleOverrideUtil::makeDetail` 이 채우고 모듈 창 View 가 그린다 — View 가 커맨드 헤더를 보지 않게 값만 따로 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 모듈 창 상세 영역 한 모듈의 내용입니다. */
    struct EditorModuleDetail
    {
        string         _name;
        string         _description;
        string         _version;
        vector<string> _listDependency;            ///< 이 모듈이 의존하는 모듈(매니페스트 순서)
        vector<string> _listDependent;             ///< 이 모듈에 직접 의존하는 모듈(이름 순)
        bool           _bEnabledByDefault{ true }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bHasOverride{ false };     ///< 프로젝트 매니페스트에 이 모듈 줄이 있다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bOverrideEnabled{ false }; ///< `_bHasOverride` 일 때 그 줄의 값 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor
