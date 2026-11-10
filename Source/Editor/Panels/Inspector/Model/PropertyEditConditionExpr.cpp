#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"

#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    PropertyEditState PropertyEditConditionExpr::getEditState( const PropertyInfo& prop, const ObjectAddress instance )
    {
        const PropertyEditConditionFunction pCondition = prop._metadata->_pEditCondition;
        if ( pCondition == nullptr || instance.get() == nullptr || pCondition( instance.get() ) )
            return PropertyEditState::Enabled;
        return prop._metadata->getEditor()._bEditConditionHides == SW_TRUE ? PropertyEditState::Hidden : PropertyEditState::Disabled;
    }
} // namespace sw
