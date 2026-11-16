/**
 * @file PropertyHandle.cpp
 * @brief (선언 타입, 색인, 세대) 프로퍼티 참조의 만들기와 풀기입니다.
 */
#include "pch.h"

#include "Engine/Reflection/PropertyHandle.h"

#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    PropertyHandle PropertyHandle::of( const TypeInfo& type, const PropertyInfo& property )
    {
        for ( const TypeInfo* pLevel : type.getTypeChain() )
        {
            const TypeInfo&             declaring = *pLevel;
            const vector<PropertyInfo>& list      = declaring._listProperty;
            if ( list.empty() == false && &property >= list.data() && &property < list.data() + list.size() )
                return PropertyHandle{ &declaring, static_cast<uint32>( &property - list.data() ), declaring._contentGeneration };
        }
        SW_FATAL( "PropertyHandle: property '%#' is not declared by %# or its bases", property._name.c_str(), type._fullyQualifiedName.c_str() );
        return PropertyHandle{};
    }

    const PropertyInfo* PropertyHandle::find() const
    {
        const bool bStale = _pType == nullptr || _pType->isAlive() == false || _pType->_contentGeneration != _generation || _index >= _pType->_listProperty.size();
        return bStale ? nullptr : &_pType->_listProperty[_index];
    }

    const PropertyInfo& PropertyHandle::get() const
    {
        const PropertyInfo* pProperty = find();
        if ( pProperty == nullptr )
            SW_FATAL( "PropertyHandle: the declaring type %# was unloaded or registered again - the property reference is stale",
                      _pType != nullptr ? _pType->_fullyQualifiedName.c_str() : "(none)" );
        return *pProperty;
    }
} // namespace sw
