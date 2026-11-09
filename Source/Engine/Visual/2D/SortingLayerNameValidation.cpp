#include "pch.h"

#include "Engine/Visual/2D/SortingLayerNameValidation.h"

#include "Core/Container/string.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    void SortingLayerNameValidation::validate( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        if ( name.empty() == false && engine::getSubsystem<Render2DSettings>().findSortingLayer( name ).has_value() == false )
            context.addErrorWithFix( string( pFieldName ) + " names '" + name.c_str() + "', which is not in the sorting layers (render2d.xml) - it draws in 'Default'",
                                     ValidationPropertyEdit{ hashed_string( pFieldName ), string(), "Clear the name - the sprite keeps drawing in 'Default' as now" } );
    }
} // namespace sw
