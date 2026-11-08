/**
 * @file SortingLayerNameValidation.h
 * @brief 컴포넌트가 이름으로 가리키는 2D 정렬 레이어(`render2d.xml` 의 `<Layer>`)가 표에 있는지 봅니다.
 * @details 런타임은 모르는 이름을 기본값으로 대신하고 로그만 남긴다. 검증 함수(`PROPERTY( Validate = fn )`)는 로드 · 저장 · 인스펙터 편집 뒤에 불려
 *          생성자 기본값까지 본다 — 데이터에 적힌 값만 보는 에셋 검증 규칙 `component_reference` 가 못 보는 자리다(언리얼 `UObject::IsDataValid`). 빈 이름은 "기본" 이라 보지 않는다.
 *          표는 서비스의 `Render2DSettings` 다(서비스가 없으면 비어 있지 않은 이름을 검증하지 못하고 멈춘다).
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
    /** @brief 컴포넌트가 이름으로 가리키는 2D 정렬 레이어(`render2d.xml` 의 `<Layer>`)가 표에 있는지 봅니다. 컴포넌트의 검증 함수가 칸 이름과 값을 넘깁니다. */
    struct SW_API SortingLayerNameValidation
    {
        /** @brief 2D 정렬 레이어 이름인지 봅니다. 없으면 `Default` 에 그려진다. */
        static void validate( ValidationContext& context, const utf8* pFieldName, const hashed_string& name );
    };
} // namespace sw
