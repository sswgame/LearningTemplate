/**
 * @file NavAreaNameValidation.h
 * @brief 컴포넌트가 이름으로 가리키는 내비게이션 영역(`navmeshsettings.xml` 의 `NavAreaDef`)이 표에 있는지 봅니다.
 * @details 런타임은 모르는 이름을 기본값으로 대신하고 로그만 남긴다. 검증 함수(`PROPERTY( Validate = fn )`)는 로드 · 저장 · 인스펙터 편집 뒤에 불려
 *          생성자 기본값까지 본다 — 데이터에 적힌 값만 보는 에셋 검증 규칙 `component_reference` 가 못 보는 자리다(언리얼 `UObject::IsDataValid`). 빈 이름은 "기본" 이라 보지 않는다.
 *          표는 `EngineDefaultAssets::_navMeshSettings` 를 한 번 읽은 것이다(`SceneNavigation::getSettings` 와 같은 파일). 한 번 읽은 표는 프로세스가 끝날 때까지 그대로다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class ValidationContext;
} // namespace sw

namespace sw
{
    /** @brief 컴포넌트가 이름으로 가리키는 내비게이션 영역(`navmeshsettings.xml` 의 `NavAreaDef`)이 표에 있는지 봅니다. 컴포넌트의 검증 함수가 칸 이름과 값을 넘깁니다. */
    struct SW_API NavAreaNameValidation
    {
        /** @brief 내비게이션 영역 이름인지 봅니다. 없으면 평지로 구워진다. */
        static void validate( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
    };
} // namespace sw
