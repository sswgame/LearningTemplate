/**
 * @file NavMeshComponentValidation.cpp
 * @brief `NavMeshLinkComponent`, `NavMeshModifierComponent` 의 프로퍼티 검증 함수(`PROPERTY( Validate = fn )`)입니다. 에디터 데이터 검증이라 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 생성 코드가 이 함수들을 `SW_DEV_ONLY_FUNCTION` 으로 등록해 배포는 참조하지 않는다(언리얼 `IsDataValid` 가 `WITH_EDITOR` 인 것과 같다).
 */
#include "pch.h"

#include "Engine/Navigation/Component/NavMeshLinkComponent.h"
#include "Engine/Navigation/Component/NavMeshModifierComponent.h"
#include "Engine/Navigation/Scene/NavAreaNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    void NavMeshLinkComponent::validateAreaName( ValidationContext& context ) const
    {
        NavAreaNameValidation::validate( context, "_areaName", _areaName );
    }

    void NavMeshModifierComponent::validateAreaName( ValidationContext& context ) const
    {
        NavAreaNameValidation::validate( context, "_areaName", _areaName );
    }
} // namespace sw
