#include "pch.h"

#include "GameFramework/Base/Data/StatBlock.h"

#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    StatValue* StatBlock::findValue( const hashed_string& name )
    {
        for ( StatValue& value : _listValue )
        {
            if ( value._name == name )
                return &value;
        }
        return nullptr;
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

    float32 StatBlock::getValue( const hashed_string& name, float32 fallback ) const
    {
        for ( const StatValue& value : _listValue )
        {
            if ( value._name == name )
                return value._value;
        }
        return fallback;
    }

    bool StatBlock::hasValue( const hashed_string& name ) const
    {
        for ( const StatValue& value : _listValue )
        {
            if ( value._name == name )
                return true;
        }
        return false;
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

    uint32 StatBlock::loadFromAttributes( const XmlNode& node, const utf8* pSkipName )
    {
        uint32 loadedCount = 0;
        for ( XmlAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const string_view name( attribute.getName() );
            bool              bSkip = false;
            if ( pSkipName != nullptr )
            {
                GameDataXml::forEachToken( string_view( pSkipName ), ",", [&]( string_view token )
                {
                    bSkip = bSkip || StringUtil::equals( token, name, true );
                } );
            }
            float32 value = 0.0f;
            if ( bSkip || StringUtil::parseFloat( string_view( attribute.getValue() ), value ) == false )
                continue; // 숫자가 아닌 속성(이름 · 태그)은 능력치가 아니다
            addValue( hashed_string( attribute.getName() ), value );
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
