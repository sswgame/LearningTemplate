/**
 * @file AbilityDebugModel.h
 * @brief 어빌리티 시스템 컴포넌트의 런타임 상태를 표 행으로 옮기는 화면 없는 모델입니다. `AbilityDebugPanel` 이 그리고, 테스트가 행을 직접 봅니다.
 * @details 언리얼 gameplay debugger 의 Ability 카테고리와 `showdebug abilitysystem` 이 보이는 것과 같다 — 어트리뷰트(base · current),
 *          걸린 이펙트(이름 · 남은 시간 · 스택), 소유 태그와 개수, 부여된 어빌리티와 쿨다운. 컴포넌트의 공개 질의만 읽고 아무것도 바꾸지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class AbilityComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 어트리뷰트 한 줄입니다. */
    struct AbilityDebugAttributeRow
    {
        string  _name{};
        float32 _baseValue{ 0.0f };
        float32 _currentValue{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 걸린 이펙트 한 줄입니다. */
    struct AbilityDebugEffectRow
    {
        string  _name{};
        float32 _remainingTime{ 0.0f }; ///< 무한 이펙트이면 0
        int32   _stackCount{ 1 };
        bool    _bInfinite{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 소유 태그 한 줄입니다. */
    struct AbilityDebugTagRow
    {
        string _tag{};
        int32  _count{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 부여된 어빌리티 한 줄입니다. */
    struct AbilityDebugAbilityRow
    {
        string  _name{};
        float32 _cooldownRemaining{ 0.0f }; ///< 쿨다운이 없으면 0
        bool    _bActive{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 컴포넌트 하나의 한 순간 상태입니다. */
    struct AbilityDebugSnapshot
    {
        vector<AbilityDebugAttributeRow> _listAttribute;
        vector<AbilityDebugEffectRow>    _listEffect;
        vector<AbilityDebugTagRow>       _listTag;
        vector<AbilityDebugAbilityRow>   _listAbility;
        float32                          _time{ 0.0f };

        void clear();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class AbilityDebugModel
     * @brief 컴포넌트 상태 → 표 행 변환입니다.
     */
    class AbilityDebugModel
    {
    public:
        /** @brief @p abilitySystem 의 지금 상태로 @p outSnapshot 을 다시 채웁니다. 지우는 중인 이펙트는 뺍니다. 행 순서는 컴포넌트의 순서(세트 · 건 순서 · 부여 순서)를 따릅니다. */
        static void collect( const AbilityComponent& abilitySystem, AbilityDebugSnapshot& outSnapshot );
    };
} // namespace sw::editor
