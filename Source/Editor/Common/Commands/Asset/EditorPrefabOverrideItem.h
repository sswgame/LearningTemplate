/**
 * @file EditorPrefabOverrideItem.h
 * @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목 — 값(모으기 · 되돌리기 동작은 `EditorToolAssetCommands.h`).
 * @details 값만 둔다: 프리팹 창 Model 이 목록을 값으로 들 때 도구 문서 커맨드를 끌어오지 않게 가른다(MVVM View include 계약, `docs/agents/EditorUI.md`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목입니다. */
    struct PrefabOverrideItem
    {
        string _componentName; ///< 화면에 보이는 이름(타입의 표시 이름)이다
        string _componentKey;  ///< 어느 컴포넌트인지 나타내는 키(`ComponentStableKey`). 같은 타입이 둘이어도 구분된다
        string _propertyName;  ///< 프로퍼티 이름. 되돌리기가 찾는 키다
        string _propertyLabel; ///< 화면에 보이는 이름이다. `DisplayName` 메타를 쓰고, 없으면 프로퍼티 이름을 다듬어 쓴다(`EditorDisplayNameUtil`). 인스펙터와 같다
        string _defaultValue;
        string _overriddenValue;
        bool   _bModified{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor
