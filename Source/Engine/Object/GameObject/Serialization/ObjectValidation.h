/**
 * @file ObjectValidation.h
 * @brief GameObject 의 컴포넌트마다 리플렉션 검증 함수(`Validate = fn`)를 돌려 결과를 `ValidationIssueLog` 에 둡니다.
 * @details 오브젝트 상태를 읽은 뒤(`ObjectStateBatch::finish`) · 저장하기 전(`ObjectStateSerializer`) · 인스펙터가 값을 고친 뒤 부릅니다.
 *          검증 함수가 없는 타입은 건너뜁니다(`ReflectionValidation::hasValidator` — 등록 때 한 번 구해 둔 답).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    class GameObject;
    class ValidationContext;
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 하나의 검증입니다. */
    struct SW_API ObjectValidation
    {
        /** @brief 컴포넌트마다 검증해 @p context 에 적고, 적힌 수를 돌려줍니다. 출처는 그 오브젝트(id · 이름)입니다. */
        static uint32 validateGameObject( const GameObject& object, ValidationContext& context );
        /**
         * @brief 검증해 그 오브젝트의 결과를 `ValidationIssueLog` 에서 바꿉니다(결과가 없으면 지운다).
         * @param bLog 결과마다 경고 로그를 남길지 — 로드 · 저장은 남기고, 프레임마다 오는 인스펙터 편집은 남기지 않는다.
         * @return 적힌 결과 수
         */
        static uint32 reportGameObject( const GameObject& object, bool bLog );
    };
} // namespace sw
