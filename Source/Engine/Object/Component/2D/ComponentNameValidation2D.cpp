#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"

// ComponentNameValidation 의 2D 정렬 레이어 몫 — 컴포넌트 모델 코어(Object/Component)는 Graphics 를 include 하지 않으므로 기능 폴더에 둔다.

namespace sw
{
    void ComponentNameValidation::validateSortingLayer( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        if ( name.empty() == false && engine::getSubsystem<Render2DSettings>().findSortingLayer( name ) < 0 )
            context.addError( string( pFieldName ) + " names '" + name.c_str() + "', which is not in the sorting layers (render2d.xml) - it draws in 'Default'" );
    }
} // namespace sw
