/**
 * @file PhysicsNameValidation.h
 * @brief 컴포넌트가 이름으로 가리키는 물리 충돌 레이어 · 재질(`physicssettings.xml`)이 표에 있는지 봅니다.
 * @details 런타임은 모르는 이름을 기본값으로 대신하고 로그만 남긴다. 검증 함수(`PROPERTY( Validate = fn )`)는 로드 · 저장 · 인스펙터 편집 뒤에 불려
 *          생성자 기본값까지 본다 — 데이터에 적힌 값만 보는 에셋 검증 규칙 `component_reference` 가 못 보는 자리다(언리얼 `UObject::IsDataValid`). 빈 이름은 "기본" 이라 보지 않는다.
 *          표는 초기화된 `PhysicsSystem` 의 설정, 없으면(도구 · 서비스 없는 테스트) `PhysicsSystem::kSettingsPath` 를 한 번 읽은 것이다. 한 번 읽은 표는 프로세스가 끝날 때까지 그대로다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    class ValidationContext;
} // namespace sw

namespace sw
{
    /** @brief 컴포넌트가 이름으로 가리키는 물리 충돌 레이어 · 재질(`physicssettings.xml`)이 표에 있는지 봅니다. 컴포넌트의 검증 함수가 칸 이름과 값을 넘깁니다. */
    struct SW_API PhysicsNameValidation
    {
        /** @brief 물리 충돌 레이어 이름(`PhysicsLayerDef`)인지 봅니다. */
        static void validateLayer( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
        /** @brief 물리 재질 이름(`PhysicsMaterialDef`)인지 봅니다. */
        static void validateMaterial( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
    };
} // namespace sw
