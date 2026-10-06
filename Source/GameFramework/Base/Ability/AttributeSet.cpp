#include "pch.h"

#include "GameFramework/Base/Ability/AttributeSet.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    AttributeSet::AttributeSet()
        : _mapAttribute{}
        , _mapRange{}
        , _listAttributeName{}
        , _pOwningAbilitySystem{ nullptr }
    {
    }

    void AttributeSet::defineAttribute( const hashed_string& name, float32 baseValue )
    {
        if ( name.empty() )
            return;

        const auto mapIter = _mapAttribute.find( name );
        if ( mapIter == _mapAttribute.end() )
            _listAttributeName.push_back( name );

        AttributeData& data = _mapAttribute[name];
        data._baseValue     = baseValue;
        data._currentValue  = baseValue;
    }

    void AttributeSet::setAttributeRange( const hashed_string& name, float32 minValue, float32 maxValue )
    {
        if ( name.empty() )
            return;
        const bool bSwapped = maxValue < minValue;
        _mapRange.insert_or_assign( name, AttributeRange{ bSwapped ? maxValue : minValue, bSwapped ? minValue : maxValue } );
    }

    bool AttributeSet::hasAttribute( const hashed_string& name ) const
    {
        return _mapAttribute.find( name ) != _mapAttribute.end();
    }

    const AttributeData* AttributeSet::findAttribute( const hashed_string& name ) const
    {
        const auto mapIter = _mapAttribute.find( name );
        return mapIter != _mapAttribute.end() ? &mapIter->second : nullptr;
    }

    void AttributeSet::preAttributeChange( const hashed_string& name, float32& inoutNewValue )
    {
        clampToRange( name, inoutNewValue );
    }

    void AttributeSet::preAttributeBaseChange( const hashed_string& name, float32& inoutNewBase )
    {
        clampToRange( name, inoutNewBase );
    }

    void AttributeSet::postAttributeChange( const hashed_string& name, float32 oldValue, float32 newValue )
    {
        (void)name;
        (void)oldValue;
        (void)newValue;
    }

    void AttributeSet::postGameplayEffectExecute( const AttributeModCallbackData& data )
    {
        (void)data;
    }

    AttributeData* AttributeSet::findAttributeMutable( const hashed_string& name )
    {
        const auto mapIter = _mapAttribute.find( name );
        return mapIter != _mapAttribute.end() ? &mapIter->second : nullptr;
    }

    void AttributeSet::clampToRange( const hashed_string& name, float32& inoutValue ) const
    {
        const auto mapIter = _mapRange.find( name );
        if ( mapIter == _mapRange.end() )
            return;
        inoutValue = MathUtil::clamp( inoutValue, mapIter->second._minValue, mapIter->second._maxValue );
    }
} // namespace sw
