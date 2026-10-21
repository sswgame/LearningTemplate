#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityAttributeSave.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/KeyValueFile.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityAttributeSave" );

    void AbilityAttributeSaveUtil::captureBaseValues( const AbilitySystemComponent& abilitySystem, vector<SavedAttributeValue>& outListValue )
    {
        for ( const unique_ptr<AttributeSet>& pAttributeSet : abilitySystem.getAttributeSets() )
        {
            for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
            {
                const AttributeData* pData = pAttributeSet->findAttribute( name );
                if ( pData == nullptr )
                    continue;
                SavedAttributeValue value;
                value._attribute = name;
                value._baseValue = pData->_baseValue;
                outListValue.push_back( value );
            }
        }
    }

    bool AbilityAttributeSaveUtil::applyBaseValues( AbilitySystemComponent& abilitySystem, const vector<SavedAttributeValue>& listValue )
    {
        bool bAllApplied = true;
        for ( const SavedAttributeValue& value : listValue )
        {
            if ( abilitySystem.setAttributeBaseValue( value._attribute, value._baseValue ) )
                continue;
            SW_LOG_ERROR( "applyBaseValues: the save has attribute '%#' but the ability system has no such attribute", value._attribute.c_str() );
            bAllApplied = false;
        }
        return bAllApplied;
    }

    bool AbilityAttributeSaveUtil::writeBytes( const vector<SavedAttributeValue>& listValue, vector<uint8>& outBytes )
    {
        KeyValueMap mapValue;
        for ( const SavedAttributeValue& value : listValue )
        {
            const string_view name        = value._attribute.view();
            const bool        bBreaksLine = name.empty() || name.find_first_of( "=\r\n#;[" ) != string_view::npos;
            if ( bBreaksLine )
            {
                SW_LOG_ERROR( "writeBytes: attribute name '%#' cannot be written as a key=value line", value._attribute.c_str() );
                return false;
            }
            mapValue[string( name )] = to_string( value._baseValue );
        }
        const string text = KeyValueFile::dump( mapValue );
        outBytes.assign( reinterpret_cast<const uint8*>( text.c_str() ), reinterpret_cast<const uint8*>( text.c_str() ) + text.size() );
        return true;
    }

    bool AbilityAttributeSaveUtil::readBytes( const uint8* pData, size_t size, vector<SavedAttributeValue>& outListValue )
    {
        KeyValueMap mapValue;
        if ( KeyValueFile::parse( string_view( reinterpret_cast<const utf8*>( pData ), size ), mapValue ) == false )
        {
            SW_LOG_ERROR( "readBytes: attribute save is not key=value text" );
            return false;
        }
        bool bValid = true;
        for ( const auto& [key, text] : mapValue )
        {
            (void)text;
            const std::optional<float32> baseValue = KeyValueFile::readRequired<float32>( mapValue, key );
            if ( baseValue.has_value() == false )
            {
                bValid = false; // readRequired 가 키와 함께 알렸다
                continue;
            }
            SavedAttributeValue value;
            value._attribute = hashed_string( string_view( key.c_str(), key.size() ) );
            value._baseValue = *baseValue;
            outListValue.push_back( value );
        }
        return bValid;
    }
} // namespace sw
