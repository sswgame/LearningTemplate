/**
 * @file ClothComponentValidation.cpp
 * @brief `ClothComponent` 의 프로퍼티 검증 함수(`PROPERTY( Validate = fn )`)입니다. 에디터 데이터 검증이라 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 생성 코드가 이 함수들을 `SW_DEV_ONLY_FUNCTION` 으로 등록해 배포는 참조하지 않는다(언리얼 `IsDataValid` 가 `WITH_EDITOR` 인 것과 같다).
 */
#include "pch.h"

#include "Engine/Physics/Cloth/ClothComponent.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    void ClothComponent::validateLOD( ValidationContext& context ) const
    {
        for ( const uint32 count : _listLODIterationCount )
        {
            if ( count >= kMinIterationCount && count <= kMaxIterationCount )
                continue;
            context.addError( string( "_listLODIterationCount has a value outside 1..32 - solver iterations must be 1 to 32" ) );
            return;
        }
    }

    void ClothComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }
} // namespace sw
