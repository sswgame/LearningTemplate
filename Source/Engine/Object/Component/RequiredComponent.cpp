#include "pch.h"

#include "Engine/Object/Component/RequiredComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    void reportMissingRequiredComponent( const Component& owner, const TypeInfo* pRequiredType )
    {
        const GameObject* pObject = owner.getOwner();
        SW_LOG_ERROR_ID( "Component.RequiredComponentMissing", "%# on '%#' requires %# on the same object - add it before this component", owner.getTypeInfo()->_name.c_str(),
                         pObject != nullptr ? pObject->getName().c_str() : "", pRequiredType != nullptr ? pRequiredType->_name.c_str() : "?" );
    }
} // namespace sw
