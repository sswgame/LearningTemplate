/**
 * @file PropertyEditConditionExpr.h
 * @brief `PROPERTY( EditCondition = "…" )` 판정으로 인스펙터가 그 프로퍼티를 숨기거나 막을지 정합니다.
 * @details 식(`name` · `!name` · `name == Value` · `name != Value`)은 생성기가 컴파일된 판정(`PropertyMetadata::_pEditCondition`)으로 낸다 —
 *          없는 이름 · 열거자는 생성 · 컴파일 오류라 여기서 풀다 실패하는 길이 없다. 에디터 기능이라 배포 구성은 짓지 않는다(Editor 빌드 단위).
 */
#pragma once
#include "Editor/Common/EditorExports.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw
{
    /** @brief 인스펙터에서 프로퍼티를 어떻게 보일지입니다. */
    enum class PropertyEditState : uint8
    {
        Enabled,  ///< 고칠 수 있다(조건이 없거나 참)
        Disabled, ///< 보이되 막는다(조건이 거짓)
        Hidden,   ///< 숨긴다(조건이 거짓이고 `EditConditionHides`)
    };

    /** @brief EditCondition 판정입니다. */
    struct SW_EDITOR_API PropertyEditConditionExpr
    {
        /** @brief @p prop 의 조건을 @p instance 값으로 판정해 보일 상태를 돌려줍니다. 조건이 없거나 인스턴스가 없으면 `Enabled` 입니다. */
        static PropertyEditState getEditState( const PropertyInfo& prop, ObjectAddress instance );
    };
} // namespace sw
