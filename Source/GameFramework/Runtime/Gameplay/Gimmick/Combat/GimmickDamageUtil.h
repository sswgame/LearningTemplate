/**
 * @file GimmickDamageUtil.h
 * @brief 기믹이 오브젝트에 피해를 주는 단일 경로입니다. 대상이 기믹 센서를 가지고 있으면 그 센서의 Damage 입력에 넣고(폭발 연쇄가 쓴다), 체력은 엔진 맞음 길
 *        (`CharacterRayHit::deliverHit` → `GameObjectEvents::_hitReceived`, `HitInfo::_kind` = 피해 종류)로 깎습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class FractureBaseComponent;
    class GameObject;

    /** @struct GimmickDamageUtil */
    struct SW_GF_API GimmickDamageUtil
    {
        /**
         * @brief @p target 에 @p amount 만큼 피해를 줍니다. 아무 스레드에서 불러도 됩니다.
         * @details 틱 중이면 틱 뒤의 게임 스레드로 미룬다. 병렬 틱에서 다른 오브젝트의 센서에 바로 넣으면, 그 센서가 이번 틱에 처리할지 다음 틱에 처리할지가
         *          스케줄에 달려서 결과가 결정적이지 않다. 연쇄 폭발과 롤백에서 문제가 된다. 미루면 항상 다음 틱에 처리한다.
         */
        static void applyDamage( const GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount );
        /** @brief 파괴 컴포넌트를 그 경계 구 크기의 폭발로 중심에서 깹니다. 조각을 깎아 내거나 통째로 부술 때 씁니다(폭발 드럼통 · 부서지는 엄폐물). */
        static void breakAtCenter( FractureBaseComponent& fracture, const float3& center, float32 strain, float32 impulse );
    };
} // namespace sw
