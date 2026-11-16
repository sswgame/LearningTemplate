#include "pch.h"

#include "Engine/UserSettings/UserSettingsFileEntry.h"

#include "Core/String/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    namespace
    {
        struct UserSettingsFileInternal
        {
            static constexpr utf8 kVersionsKey[] = "versions";
            /** @brief 옛 형식(주인별 판 이전)의 판 번호 하나입니다. 읽기만 한다 — `parseJSON` 의 이주 길. */
            static constexpr utf8 kLegacyVersionKey[] = "version";
            static constexpr utf8 kValuesKey[]        = "values";
            /** @brief 판 번호가 없는 파일(판 번호를 쓰기 전에 저장한 파일) · 파일에 없는 주인의 판입니다. 모든 올림 단계를 거친다. */
            static constexpr uint32 kUnversionedFile = 0;

            /** @brief JSON 값을 텍스트로 바꿉니다. 숫자는 쓰인 그대로, 불리언은 true/false 로 씁니다. */
            static string toText( const JSONValue& value )
            {
                switch ( value.getType() )
                {
                    case JSONType::Bool:
                        return value.read<bool>() == true ? "true" : "false";
                    case JSONType::Number:
                    case JSONType::String:
                        return value.asString(); // 숫자는 쓰인 그대로다
                    case JSONType::Null:
                    case JSONType::Array:
                    case JSONType::Object:
                        return {};
                }
                return {};
            }

            static bool equalsText( string_view lhs, string_view rhs ) { return StringUtil::equals( lhs, rhs ); }

            /**
             * @brief 파일이 적은 주인 @p owner 의 판입니다. 없으면 0 이고, 판이 부호 없는 정수가 아니면 비어 있습니다(형식 오류).
             * @details 옛 형식은 판 하나(모든 스키마의 최댓값)라 주인의 판보다 크면 주인의 판으로 줄인다 — 주인별 판으로 바꿀 때 저장소의 스키마는 모두 판 1 이었으므로
             *          옛 파일의 번호(0 · 1)는 그대로 각 주인의 판이고, 줄이기는 큰 번호를 "이 빌드보다 새 파일" 로 잘못 보지 않게 한다.
             */
            static optional<uint32> readOwnerVersion( const JSONValue& root, const UserSettingsSchemaOwner& owner )
            {
                const JSONValue versions = root.get( kVersionsKey );
                if ( versions.isObject() )
                {
                    const JSONValue version = versions.get( owner._owner.c_str() );
                    if ( version.isValid() == false || version.isNull() )
                        return kUnversionedFile;
                    return version.read<uint32>();
                }
                uint32 legacyVersion{ kUnversionedFile };
                if ( root.readOptionalInto( kLegacyVersionKey, legacyVersion ) == false )
                    return nullopt;
                return legacyVersion < owner._version ? legacyVersion : owner._version;
            }

            /** @brief 주인 @p owner 의 판 @p fromVersion 단계를 적힌 순서대로 목록에 적용합니다. */
            static void applyUpgradeSteps( const UserSettingsSchemaOwner& owner, uint32 fromVersion, vector<UserSettingsFileEntry>& inoutListEntry,
                                           [[maybe_unused]] string_view sourceName )
            {
                for ( const UserSettingsUpgradeStep& step : owner._listUpgradeStep )
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
                                {
                                    entry._value = step._toValue;
                                    entry._rawJSON.clear();
                                }
                                break;
                            }
                            case UserSettingsUpgradeOperation::Scale:
                            {
                                float64 number{ 0.0 };
                                if ( StringUtil::parseDouble( entry._value, number ) )
                                {
                                    utf8 arrBuffer[constant::buffer::kMax64]{};
                                    StringUtil::formatNumber( arrBuffer, constant::buffer::kMax64, number * step._scale );
                                    entry._value = arrBuffer;
                                    entry._rawJSON.clear();
                                }
                                break;
                            }
                        }
                    }
                }
                SW_LOG_TRACE_ID( "UserSettings.UpgradedUserSettingsVersion", "%#: upgraded user settings '%#' from version %#", sourceName, owner._owner.c_str(), fromVersion );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UserSettingsFileReadResult UserSettingsFileEntry::parseJSON( string_view jsonText, string_view sourceName, const UserSettingsSchema& schema,
                                                                 UserSettingsFileContent& outContent )
    {
        using Internal = UserSettingsFileInternal;
        outContent     = {};
        JSONDocument doc;
        if ( doc.parse( jsonText, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "UserSettings.UserSettingsNotValid", "User settings '%#' is not valid JSON: %#", sourceName, doc.getLastError() );
            return UserSettingsFileReadResult::Unreadable;
        }
        const JSONValue root     = doc.getRoot();
        const JSONValue values   = root.get( Internal::kValuesKey );
        const JSONValue versions = root.get( Internal::kVersionsKey );
        if ( root.isObject() == false || values.isObject() == false )
        {
            SW_LOG_ERROR_ID( "UserSettings.UserSettingsNoValues", "User settings '%#' has no \"values\" object", sourceName );
            return UserSettingsFileReadResult::Unreadable;
        }
        if ( versions.isValid() && versions.isNull() == false && versions.isObject() == false )
        {
            SW_LOG_ERROR_ID( "UserSettings.UserSettingsVersionsNotObject", "User settings '%#' has a \"versions\" that is not an object", sourceName );
            return UserSettingsFileReadResult::Unreadable;
        }

        // 주인마다 판을 먼저 다 읽는다 — 한 주인이라도 이 빌드보다 새면 내용을 하나도 쓰지 않는다.
        const vector<UserSettingsSchemaOwner>& listOwner = schema.getOwners();
        vector<uint32>                         listFileVersion;
        for ( const UserSettingsSchemaOwner& owner : listOwner )
        {
            const optional<uint32> fileVersion = Internal::readOwnerVersion( root, owner );
            if ( fileVersion.has_value() == false )
            {
                SW_LOG_ERROR_ID( "UserSettings.UserSettingsVersionNot", "User settings '%#' has a version for '%#' that is not an unsigned integer", sourceName,
                                 owner._owner.c_str() );
                return UserSettingsFileReadResult::Unreadable;
            }
            if ( *fileVersion > owner._version )
            {
                SW_LOG_WARNING_ID( "UserSettings.UserSettingsVersionNewer", "User settings '%#' has '%#' version %#, newer than this build (%#) - the file is not used",
                                   sourceName, owner._owner.c_str(), *fileVersion, owner._version );
                return UserSettingsFileReadResult::NewerThanBuild;
            }
            listFileVersion.push_back( *fileVersion );
        }
        if ( versions.isObject() )
        {
            for ( const string& ownerName : versions.getMemberNames() )
            {
                const hashed_string owner( string_view{ ownerName } );
                if ( schema.findOwner( owner ) != nullptr )
                    continue;
                const optional<uint32> version = versions.get( ownerName ).read<uint32>();
                if ( version.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "UserSettings.UserSettingsVersionNot", "User settings '%#' has a version for '%#' that is not an unsigned integer", sourceName, ownerName );
                    return UserSettingsFileReadResult::Unreadable;
                }
                outContent._listForeignOwner.push_back( UserSettingsFileOwnerVersion{ owner, *version } );
            }
        }

        for ( const string& key : values.getMemberNames() )
        {
            const JSONValue        value = values.get( key );
            const expected<string> raw   = value.dump();
            outContent._listEntry.push_back( UserSettingsFileEntry{ key, Internal::toText( value ), raw.has_value() ? *raw : string{} } );
        }
        for ( size_t ownerIndex = 0; ownerIndex < listOwner.size(); ++ownerIndex )
        {
            for ( uint32 version = listFileVersion[ownerIndex]; version < listOwner[ownerIndex]._version; ++version )
            {
                Internal::applyUpgradeSteps( listOwner[ownerIndex], version, outContent._listEntry, sourceName );
            }
        }
        return UserSettingsFileReadResult::Read;
    }

    expected<string> UserSettingsFileEntry::makeJSON( const UserSettingsSchema& schema, const vector<UserSettingsWriteEntry>& listEntry,
                                                      const UserSettingsFileContent& carried )
    {
        using Internal = UserSettingsFileInternal;
        JSONDocument    doc;
        const JSONValue root     = doc.makeObject();
        const JSONValue versions = root.set( Internal::kVersionsKey );
        versions.setObject();
        for ( const UserSettingsSchemaOwner& owner : schema.getOwners() )
        {
            versions.set( owner._owner.c_str() ).setUint( owner._version );
        }
        for ( const UserSettingsFileOwnerVersion& foreign : carried._listForeignOwner )
        {
            versions.set( foreign._owner.c_str() ).setUint( foreign._version );
        }
        const JSONValue values = root.set( Internal::kValuesKey );
        values.setObject();

        // 이 빌드가 모르는 키는 원문 그대로 다시 쓴다(다른 구성 · 다른 판 빌드의 값). 원문이 없으면(올림 단계가 바꿨다) 글로 쓴다.
        for ( const UserSettingsFileEntry& entry : carried._listEntry )
        {
            const JSONValue field = values.set( entry._key );
            JSONDocument    rawDoc;
            if ( entry._rawJSON.empty() == false && rawDoc.tryParse( entry._rawJSON ) )
                field.assignFrom( rawDoc.getRoot() );
            else
                field.setString( entry._value );
        }

        for ( const UserSettingsWriteEntry& entry : listEntry )
        {
            const JSONValue field = values.set( entry._def._id.c_str() );
            switch ( UserSettingsSchema::getTypeTraits( entry._def._type )._storageKind )
            {
                case UserSettingStorageKind::Bool:
                {
                    bool bValue{ false };
                    if ( StringUtil::tryParseBool( entry._value, bValue ) )
                        field.setBool( bValue );
                    else
                        field.setString( entry._value ); // 읽지 못한 글을 false 로 바꿔 저장하지 않는다 — 다음 읽기가 거절하고 알린다
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
