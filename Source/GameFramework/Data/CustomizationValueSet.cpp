#include "pch.h"

#include "GameFramework/Data/CustomizationValueSet.h"

namespace sw
{
    CustomizationValue* CustomizationValueSet::findMutableValue( const hashed_string& parameter )
    {
        for ( CustomizationValue& value : _listValue )
        {
            if ( value._parameter == parameter )
                return &value;
        }
        return nullptr;
    }

    const CustomizationValue* CustomizationValueSet::findValue( const hashed_string& parameter ) const
    {
        for ( const CustomizationValue& value : _listValue )
        {
            if ( value._parameter == parameter )
                return &value;
        }
        return nullptr;
    }

    void CustomizationValueSet::setValue( const CustomizationValue& value )
    {
        if ( value._parameter.empty() )
            return;
        CustomizationValue* pValue = findMutableValue( value._parameter );
        if ( pValue != nullptr )
            *pValue = value;
        else
            _listValue.push_back( value );
    }

    void CustomizationValueSet::setNumber( const hashed_string& parameter, float32 value )
    {
        CustomizationValue entry;
        entry._parameter = parameter;
        entry._number    = float4( value, 0.0f, 0.0f, 0.0f );
        setValue( entry );
    }

    void CustomizationValueSet::setColor( const hashed_string& parameter, const float4& color )
    {
        CustomizationValue entry;
        entry._parameter = parameter;
        entry._number    = color;
        setValue( entry );
    }

    void CustomizationValueSet::setOption( const hashed_string& parameter, const hashed_string& option )
    {
        CustomizationValue entry;
        entry._parameter = parameter;
        entry._option    = option;
        setValue( entry );
    }

    bool CustomizationValueSet::removeValue( const hashed_string& parameter )
    {
        for ( size_t index = 0; index < _listValue.size(); ++index )
        {
            if ( _listValue[index]._parameter == parameter )
            {
                _listValue.erase( _listValue.begin() + static_cast<ptrdiff_t>( index ) );
                return true;
            }
        }
        return false;
    }

    void CustomizationValueSet::overlay( const CustomizationValueSet& other )
    {
        for ( const CustomizationValue& value : other._listValue )
        {
            setValue( value );
        }
    }

    bool CustomizationValueSet::isEquivalent( const CustomizationValueSet& other ) const
    {
        if ( _listValue.size() != other._listValue.size() )
            return false;
        for ( const CustomizationValue& value : _listValue )
        {
            const CustomizationValue* pOther = other.findValue( value._parameter );
            if ( pOther == nullptr || ( *pOther == value ) == false )
                return false;
        }
        return true;
    }
} // namespace sw
