#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityAttributeSave.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityAttributeSave" );

    void AbilityAttributeSaveUtil::captureBaseValues( const AbilityComponent& abilitySystem, vector<SavedAttributeValue>& outListValue )
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

    bool AbilityAttributeSaveUtil::applyBaseValues( AbilityComponent& abilitySystem, const vector<SavedAttributeValue>& listValue )
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
        for ( const SavedAttributeValue& value : listValue )
        {
            if ( value._attribute.view().empty() )
            {
                SW_LOG_ERROR( "writeBytes: an attribute without a name cannot be saved" );
                return false;
            }
        }
        SavedAttributeFile file;
        file._listValue   = listValue;
        const string text = JSONSerializer::serialize( &file, *SavedAttributeFile::StaticType() );
        outBytes.assign( reinterpret_cast<const uint8*>( text.c_str() ), reinterpret_cast<const uint8*>( text.c_str() ) + text.size() );
        return true;
    }

    bool AbilityAttributeSaveUtil::readBytes( const uint8* pData, size_t size, vector<SavedAttributeValue>& outListValue )
    {
        SavedAttributeFile file;
        // 모르는 키 · 빠진 필수 필드 · 읽지 못한 값은 키 이름과 함께 오류로 남는다(옛 `key=value` 세이브도 여기서 진다).
        if ( ConfigManager::readConfigJSON( file, string_view( reinterpret_cast<const utf8*>( pData ), size ), "ability attribute save" ) == false )
            return false;
        outListValue.insert( outListValue.end(), file._listValue.begin(), file._listValue.end() );
        return true;
    }
} // namespace sw
