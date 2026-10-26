#include "pch.h"

#include "Engine/UserSettings/UserSettingsFile.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettings" );

    namespace
    {
        struct UserSettingsFileInternal
        {
            static constexpr utf8 kVersionKey[] = "version";
            static constexpr utf8 kValuesKey[]  = "values";
            /** @brief 판 번호가 없는 파일(판 번호를 쓰기 전에 저장한 파일)의 판입니다. 모든 올림 단계를 거친다. */
            static constexpr uint32 kUnversionedFile = 0;

            /** @brief JSON 값을 텍스트로 바꿉니다. 숫자는 쓰인 그대로, 불리언은 true/false 로 씁니다. */
            static string toText( const JSONValue& value )
            {
                switch ( value.getType() )
                {
                    case JSONType::Bool:
                        return value.read<bool>() == true ? "true" : "false";
                    case JSONType::Number:
                        return value.dump();
                    case JSONType::String:
                        return value.asString();
                    case JSONType::Null:
                    case JSONType::Array:
                    case JSONType::Object:
                        return {};
                }
                return {};
            }

            static bool equalsText( string_view lhs, string_view rhs ) { return StringUtil::equals( lhs, rhs ); }

            /** @brief 버전 @p fromVersion 의 단계를 적힌 순서대로 목록에 적용합니다. */
            static void applyUpgradeSteps( const UserSettingsSchema& schema, uint32 fromVersion, vector<UserSettingsFileEntry>& inoutListEntry,
                                           [[maybe_unused]] string_view sourceName )
            {
                for ( const UserSettingsUpgradeStep& step : schema.getUpgradeSteps() )
                {
                    if ( step._fromVersion != fromVersion )
                        continue;
                    for ( size_t entryIndex = inoutListEntry.size(); entryIndex > 0; --entryIndex )
                    {
                        UserSettingsFileEntry& entry = inoutListEntry[entryIndex - 1];
                        if ( equalsText( entry._key, step._key ) == false )
                            continue;
                        switch ( step._operation )
                        {
                            case UserSettingsUpgradeOperation::Rename:
                            {
                                entry._key = step._newKey;
                                break;
                            }
                            case UserSettingsUpgradeOperation::Remove:
                            {
                                inoutListEntry.erase( inoutListEntry.begin() + static_cast<ptrdiff_t>( entryIndex - 1 ) );
                                break;
                            }
                            case UserSettingsUpgradeOperation::MapValue:
                            {
                                if ( equalsText( entry._value, step._fromValue ) )
                                    entry._value = step._toValue;
                                break;
                            }
                            case UserSettingsUpgradeOperation::Scale:
                            {
                                float64 number{ 0.0 };
                                if ( StringUtil::parseDouble( entry._value, number ) )
                                {
                                    utf8 arrBuffer[constant::kMaxBuffer64]{};
                                    StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, number * step._scale );
                                    entry._value = arrBuffer;
                                }
                                break;
                            }
                        }
                    }
                }
                SW_LOG_TRACE( "%#: upgraded user settings from version %#", sourceName, fromVersion );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UserSettingsFile::parseJSON( string_view jsonText, string_view sourceName, const UserSettingsSchema& schema, vector<UserSettingsFileEntry>& outListEntry )
    {
        using Internal = UserSettingsFileInternal;
        outListEntry.clear();
        JSONDocument doc;
        if ( doc.parse( jsonText, sourceName ) == false )
        {
            SW_LOG_ERROR( "User settings '%#' is not valid JSON: %#", sourceName, doc.getLastError() );
            return false;
        }
        const JSONValue root   = doc.getRoot();
        const JSONValue values = root.get( Internal::kValuesKey );
        if ( root.isObject() == false || values.isObject() == false )
        {
            SW_LOG_ERROR( "User settings '%#' has no \"values\" object", sourceName );
            return false;
        }

        for ( const string& key : values.getMemberNames() )
        {
            outListEntry.push_back( UserSettingsFileEntry{ key, Internal::toText( values.get( key ) ) } );
        }

        uint32 fileVersion{ Internal::kUnversionedFile };
        if ( root.readOptionalInto( Internal::kVersionKey, fileVersion ) == false )
        {
            SW_LOG_ERROR( "User settings '%#' has a \"version\" that is not an unsigned integer", sourceName );
            return false;
        }
        const uint32 schemaVersion = schema.getVersion();
        for ( uint32 version = fileVersion; version < schemaVersion; ++version )
        {
            Internal::applyUpgradeSteps( schema, version, outListEntry, sourceName );
        }
        if ( fileVersion > schemaVersion )
            SW_LOG_WARNING( "User settings '%#' is version %#, newer than this build (%#) - keys this build does not know are dropped", sourceName, fileVersion, schemaVersion );
        return true;
    }

    string UserSettingsFile::makeJSON( const UserSettingsSchema& schema, const vector<UserSettingsFileEntry>& listEntry )
    {
        using Internal = UserSettingsFileInternal;
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();
        root.set( Internal::kVersionKey ).setUint( schema.getVersion() );
        const JSONValue values = root.set( Internal::kValuesKey );
        values.setObject();

        for ( const UserSettingsFileEntry& entry : listEntry )
        {
            const UserSettingDef* pDef  = schema.findSetting( hashed_string( entry._key ) );
            const UserSettingType type  = pDef != nullptr ? pDef->_type : UserSettingType::String;
            const JSONValue       field = values.set( entry._key.c_str() );
            switch ( UserSettingsSchema::getTypeTraits( type )._storageKind )
            {
                case UserSettingStorageKind::Bool:
                {
                    field.setBool( StringUtil::parseBool( entry._value, false ) );
                    break;
                }
                case UserSettingStorageKind::Number:
                {
                    float64 number{ 0.0 };
                    if ( StringUtil::parseDouble( entry._value, number ) )
                        field.setFloat( number );
                    else
                        field.setString( entry._value );
                    break;
                }
                case UserSettingStorageKind::Text:
                {
                    field.setString( entry._value );
                    break;
                }
            }
        }
        return doc.dump( 4 );
    }
} // namespace sw
