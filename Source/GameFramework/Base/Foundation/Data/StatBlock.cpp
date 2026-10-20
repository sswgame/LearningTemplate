#include "pch.h"

#include "GameFramework/Base/Foundation/Data/StatBlock.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    StatValue* StatBlock::findValue( const hashed_string& name )
    {
        return VectorUtil::findBy<&StatValue::_name>( _listValue, name );
    }

    void StatBlock::setValue( const hashed_string& name, float32 value )
    {
        StatValue* pValue = findValue( name );
        if ( pValue != nullptr )
            pValue->_value = value;
        else
            _listValue.push_back( StatValue{ name, value } );
    }

    void StatBlock::addValue( const hashed_string& name, float32 value )
    {
        StatValue* pValue = findValue( name );
        if ( pValue != nullptr )
            pValue->_value += value;
        else
            _listValue.push_back( StatValue{ name, value } );
    }

    float32 StatBlock::getValue( const hashed_string& name ) const
    {
        const std::optional<float32> value = findValue( name );
        return value.has_value() ? *value : 0.0f; // 더하는 묶음이라 없는 이름의 기여는 0 이다(합의 항등원)
    }

    float32 StatBlock::getScale( const hashed_string& name ) const
    {
        const std::optional<float32> value = findValue( name );
        return value.has_value() ? *value : 1.0f; // 곱하는 묶음이라 없는 이름은 보정이 없다(곱의 항등원)
    }

    std::optional<float32> StatBlock::findValue( const hashed_string& name ) const
    {
        for ( const StatValue& value : _listValue )
        {
            if ( value._name == name )
                return value._value;
        }
        return std::nullopt;
    }

    bool StatBlock::hasValue( const hashed_string& name ) const
    {
        return VectorUtil::containsBy<&StatValue::_name>( _listValue, name );
    }

    bool StatBlock::canAfford( const StatBlock& cost ) const
    {
        for ( const StatValue& value : cost.getValues() )
        {
            if ( getValue( value._name ) + 1.0e-4f < value._value )
                return false;
        }
        return true;
    }

    bool StatBlock::trySpend( const StatBlock& cost )
    {
        if ( canAfford( cost ) == false )
            return false;
        merge( cost, -1.0f );
        return true;
    }

    void StatBlock::merge( const StatBlock& other, float32 scale )
    {
        for ( const StatValue& value : other._listValue )
        {
            addValue( value._name, value._value * scale );
        }
    }

    uint32 StatBlock::loadFromAttributes( const XMLNode& node, const utf8* pSkipName )
    {
        uint32 loadedCount = 0;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const string_view name( attribute.getName() );
            bool              bSkip = false;
            if ( pSkipName != nullptr )
            {
                StringUtil::forEachToken( string_view( pSkipName ), ",", [&]( string_view token )
                {
                    bSkip = bSkip || StringUtil::equals( token, name, true );
                } );
            }
            float32 statValue = 0.0f;
            if ( bSkip || StringUtil::parseFloat( string_view( attribute.getValue() ), statValue ) == false )
                continue; // 숫자가 아닌 속성(이름 · 태그)은 능력치가 아니다
            addValue( hashed_string( attribute.getName() ), statValue );
            ++loadedCount;
        }
        return loadedCount;
    }

    void StatBlock::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listValue.size() );
        for ( const StatValue& value : _listValue )
        {
            StateArchiveUtil::writeName( outArchive, value._name );
            outArchive << value._value;
        }
    }

    bool StatBlock::readState( Archive& archive )
    {
        uint32 count = 0;
        // 칸마다 이름 길이(4) + 값(4) 이상
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        vector<StatValue> listValue( count );
        for ( StatValue& value : listValue )
        {
            if ( StateArchiveUtil::readName( archive, value._name ) == false )
                return false;
            archive >> value._value;
            if ( archive.isError() || value._name.empty() )
                return false;
        }
        _listValue = std::move( listValue );
        return true;
    }
} // namespace sw
