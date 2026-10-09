#include "pch.h"

#include "Engine/UserSettings/UserSettingsManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Serialization/Json/JsonDocument.h"
#include "Engine/UserSettings/HardwareProbe.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettings" );

    namespace
    {
        struct UserSettingsManagerInternal
        {
            static constexpr utf8 kFileName[]   = "usersettings.json";
            static constexpr utf8 kVersionKey[] = "version";
            static constexpr utf8 kValuesKey[]  = "values";

            /** @brief 사용자 파일의 키 · 값 한 줄입니다(버전 올리기가 이 목록을 고친다). */
            struct FileEntry
            {
                string _key;
                string _value;
            };

            /** @brief JSON 값을 글로 바꿉니다 — 숫자는 쓰인 그대로, 불리언은 true/false. */
            static string toText( const JsonValue& value )
            {
                switch ( value.getType() )
                {
                    case JsonType::Bool:
                        return value.asBool() ? "true" : "false";
                    case JsonType::Number:
                        return value.dump();
                    case JsonType::String:
                        return value.asString();
                    case JsonType::Null:
                    case JsonType::Array:
                    case JsonType::Object:
                        return {};
                }
                return {};
            }

            static bool equalsText( string_view lhs, string_view rhs ) { return StringUtil::equals( lhs, rhs, true ); }

            /** @brief 판 @p fromVersion 의 단계를 적힌 순서대로 목록에 적용합니다. */
            static void applyUpgradeSteps( const UserSettingsSchema& schema, uint32 fromVersion, vector<FileEntry>& inoutListEntry,
                                           [[maybe_unused]] string_view sourceName )
            {
                for ( const UserSettingsUpgradeStep& step : schema.getUpgradeSteps() )
                {
                    if ( step._fromVersion != fromVersion )
                        continue;
                    for ( size_t entryIndex = inoutListEntry.size(); entryIndex > 0; --entryIndex )
                    {
                        FileEntry& entry = inoutListEntry[entryIndex - 1];
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

            /** @brief 열거형 선택지의 순번입니다(전역 변수에 넘길 기본 글). */
            static uint32 findOptionIndex( const UserSettingDef& def, string_view value )
            {
                for ( uint32 optionIndex = 0; optionIndex < static_cast<uint32>( def._listOption.size() ); ++optionIndex )
                {
                    if ( equalsText( def._listOption[optionIndex]._value, value ) )
                        return optionIndex;
                }
                return invalid_index::kUint32;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UserSettingsManager::UserSettingsManager()
        : _schema{}
        , _registry{}
        , _engineAppliers{}
        , _listState{}
        , _onEvent{}
        , _userFilePath{}
        , _confirmSecondsLeft{ 0.0f }
        , _bUserFileLoaded{ false }
        , _bStartupApplied{ false }
    {
    }

    UserSettingsManager::~UserSettingsManager() = default;

    void UserSettingsManager::initialize( const UserSettingsTargets& targets )
    {
        _engineAppliers.initialize( targets, _registry );
    }

    void UserSettingsManager::shutdown()
    {
        _engineAppliers.shutdown();
        _registry.clear();
        _schema.clear();
        _listState.clear();
        _onEvent.clear();
        _userFilePath.clear();
        _confirmSecondsLeft = 0.0f;
        _bUserFileLoaded    = false;
        _bStartupApplied    = false;
    }

    // ------------------------------------------------------------------------------
    // 1) 스키마
    // ------------------------------------------------------------------------------
    bool UserSettingsManager::loadSchema( string_view resourcePath )
    {
        const UserSettingsSchema backup  = _schema;
        const bool               bLoaded = _schema.loadFromResource( resourcePath );
        if ( appendSchema( bLoaded ) )
            return true;
        _schema = backup;
        return false;
    }

    bool UserSettingsManager::loadSchemaFromXmlText( string_view xmlText, string_view sourceName )
    {
        const UserSettingsSchema backup  = _schema;
        const bool               bLoaded = _schema.loadFromXmlText( xmlText, sourceName );
        if ( appendSchema( bLoaded ) )
            return true;
        _schema = backup;
        return false;
    }

    bool UserSettingsManager::appendSchema( bool bLoaded )
    {
        if ( bLoaded == false || _schema.validate( _registry, getTargets()._pGlobalVariableManager ) == false )
            return false;

        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( size_t settingIndex = _listState.size(); settingIndex < listSetting.size(); ++settingIndex )
        {
            SettingState state;
            state._defaultValue   = listSetting[settingIndex]._defaultValue;
            state._committedValue = state._defaultValue;
            _listState.push_back( state );
        }

        // 품질 묶음의 기본 프리셋이 묶인 설정의 기본값이다 — 두 곳에 적은 기본값이 어긋나 첫 화면이 Custom 이 되는 일이 없게.
        for ( const ScalabilityGroupDef& group : _schema.getScalabilityGroups() )
        {
            const uint32 groupIndex = findIndex( group._settingId );
            if ( groupIndex == invalid_index::kUint32 )
                continue;
            const hashed_string presetName( _listState[groupIndex]._defaultValue );
            for ( const ScalabilityPresetDef& preset : group._listPreset )
            {
                if ( preset._name != presetName )
                    continue;
                for ( const ScalabilityPresetValue& value : preset._listValue )
                {
                    const uint32 memberIndex = findIndex( value._settingId );
                    string       normalized;
                    if ( memberIndex == invalid_index::kUint32 ||
                         UserSettingsSchema::normalizeValue( listSetting[memberIndex], value._value, normalized ) != UserSettingValueResult::Accepted )
                        continue;
                    SettingState& member = _listState[memberIndex];
                    if ( member._committedValue == member._defaultValue )
                        member._committedValue = normalized;
                    member._defaultValue = normalized;
                }
            }
        }
        return true;
    }

    bool UserSettingsManager::setGameDefault( const hashed_string& settingId, string_view value )
    {
        const uint32 settingIndex = findIndex( settingId );
        if ( settingIndex == invalid_index::kUint32 )
        {
            SW_LOG_ERROR( "game default for an unknown setting '%#'", settingId.c_str() );
            return false;
        }
        string normalized;
        if ( UserSettingsSchema::normalizeValue( _schema.getSettings()[settingIndex], value, normalized ) != UserSettingValueResult::Accepted )
        {
            SW_LOG_ERROR( "game default '%#' for '%#' is not a valid value", value, settingId.c_str() );
            return false;
        }
        SettingState& state = _listState[settingIndex];
        if ( state._committedValue == state._defaultValue )
            state._committedValue = normalized;
        state._defaultValue = normalized;

        const ScalabilityGroupDef* pGroup = _schema.findScalabilityGroupOf( settingId );
        if ( pGroup != nullptr && pGroup->_settingId == settingId )
            applyScalabilityPreset( *pGroup, hashed_string( normalized ), true );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 2) 사용자 파일
    // ------------------------------------------------------------------------------
    bool UserSettingsManager::loadUserFile( string_view filePath )
    {
        if ( filePath.empty() || FileUtil::exists( filePath ) == false )
            return false;
        string text;
        if ( FileUtil::readTextFile( filePath, text ) == false )
        {
            SW_LOG_WARNING( "Failed to read user settings '%#' - using defaults", filePath );
            return false;
        }
        return loadUserJson( text, filePath );
    }

    bool UserSettingsManager::loadUserJson( string_view jsonText, string_view sourceName )
    {
        using Internal = UserSettingsManagerInternal;
        JsonDocument doc;
        if ( doc.parse( jsonText, sourceName ) == false )
        {
            SW_LOG_WARNING( "User settings '%#' is not valid JSON - using defaults", sourceName );
            return false;
        }
        const JsonValue root   = doc.getRoot();
        const JsonValue values = root.get( Internal::kValuesKey );
        if ( root.isObject() == false || values.isObject() == false )
        {
            SW_LOG_WARNING( "User settings '%#' has no \"values\" object - using defaults", sourceName );
            return false;
        }

        vector<Internal::FileEntry> listEntry;
        for ( const string& key : values.getMemberNames() )
        {
            listEntry.push_back( Internal::FileEntry{ key, Internal::toText( values.get( key, false ) ) } );
        }

        // 버전 올리기 — 배포된 플레이어 데이터라 옛 판을 읽어야 한다(게임 데이터와 달리 다시 쓸 수 없다).
        const uint32 fileVersion   = static_cast<uint32>( root.get( Internal::kVersionKey ).asUint( 0 ) );
        const uint32 schemaVersion = _schema.getVersion();
        for ( uint32 version = fileVersion; version < schemaVersion; ++version )
        {
            Internal::applyUpgradeSteps( _schema, version, listEntry, sourceName );
        }
        if ( fileVersion > schemaVersion )
            SW_LOG_WARNING( "User settings '%#' is version %#, newer than this build (%#) - keys this build does not know are dropped", sourceName, fileVersion, schemaVersion );

        for ( SettingState& state : _listState )
        {
            state._committedValue = state._defaultValue;
            state._pendingValue.clear();
            state._bPending  = false;
            state._bRollback = false;
        }
        for ( const Internal::FileEntry& entry : listEntry )
        {
            const uint32 settingIndex = findIndex( hashed_string( entry._key ) );
            if ( settingIndex == invalid_index::kUint32 )
            {
                SW_LOG_WARNING( "User settings '%#': dropped unknown key '%#'", sourceName, entry._key );
                continue;
            }
            string                       normalized;
            const UserSettingValueResult result = UserSettingsSchema::normalizeValue( _schema.getSettings()[settingIndex], entry._value, normalized );
            if ( result == UserSettingValueResult::Rejected )
            {
                SW_LOG_WARNING( "User settings '%#': '%#' has an invalid value '%#' - using the default", sourceName, entry._key, entry._value );
                continue;
            }
            if ( result == UserSettingValueResult::Clamped )
                SW_LOG_WARNING( "User settings '%#': '%#' = '%#' is out of range - using '%#'", sourceName, entry._key, entry._value, normalized );
            _listState[settingIndex]._committedValue = normalized;
        }
        for ( const ScalabilityGroupDef& group : _schema.getScalabilityGroups() )
        {
            refreshScalabilityGroup( group, true );
        }

        _bUserFileLoaded = true;
        broadcast( {}, UserSettingEventKind::Loaded );
        return true;
    }

    string UserSettingsManager::makeUserJson() const
    {
        using Internal = UserSettingsManagerInternal;
        JsonDocument    doc;
        const JsonValue root = doc.makeObject();
        root.set( Internal::kVersionKey ).setUint( _schema.getVersion() );
        const JsonValue values = root.set( Internal::kValuesKey );
        values.setObject();

        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( size_t settingIndex = 0; settingIndex < _listState.size(); ++settingIndex )
        {
            const SettingState& state = _listState[settingIndex];
            // 확인 대기 중이면 옛 값을 쓴다 — 확인 전에 꺼지면 다음 실행은 확인된 화면으로 뜬다.
            const string& value = state._bRollback ? state._rollbackValue : state._committedValue;
            if ( value == state._defaultValue )
                continue;
            const UserSettingDef& def   = listSetting[settingIndex];
            const JsonValue       field = values.set( def._id.c_str(), false );
            switch ( def._type )
            {
                case UserSettingType::Bool:
                {
                    field.setBool( StringUtil::parseBool( value, false ) );
                    break;
                }
                case UserSettingType::Int:
                case UserSettingType::Float:
                {
                    float64 number{ 0.0 };
                    if ( StringUtil::parseDouble( value, number ) )
                        field.setFloat( number );
                    else
                        field.setString( value );
                    break;
                }
                case UserSettingType::Enum:
                case UserSettingType::KeyBinding:
                case UserSettingType::String:
                {
                    field.setString( value );
                    break;
                }
            }
        }
        return doc.dump( 4 );
    }

    bool UserSettingsManager::saveUserFile( string_view filePath ) const
    {
        if ( filePath.empty() )
            return false;
        if ( FileUtil::ensureParentDirectoryExists( filePath ) == false )
        {
            SW_LOG_WARNING( "Cannot create the folder for user settings '%#'", filePath );
            return false;
        }
        if ( FileUtil::writeTextFile( filePath, makeUserJson() ) == false )
        {
            SW_LOG_WARNING( "Failed to write user settings '%#'", filePath );
            return false;
        }
        return true;
    }

    string UserSettingsManager::makeDefaultUserFilePath( string_view gameName )
    {
        return FileUtil::joinPath( UserDataPath::getConfigDirectory( gameName ), UserSettingsManagerInternal::kFileName );
    }

    // ------------------------------------------------------------------------------
    // 3) 기동
    // ------------------------------------------------------------------------------
    hashed_string UserSettingsManager::detectScalabilityPreset( const HardwareProbeResult& probe ) const
    {
        const vector<ScalabilityGroupDef>& listGroup = _schema.getScalabilityGroups();
        if ( listGroup.empty() )
            return {};
        for ( const ScalabilityAutoDetectRule& rule : listGroup.front()._listAutoDetectRule )
        {
            const bool bMatches = probe._logicalCoreCount >= rule._minLogicalCoreCount && probe._systemMemoryMb >= rule._minSystemMemoryMb;
            if ( bMatches )
                return rule._preset;
        }
        return listGroup.front()._autoDetectFallback;
    }

    bool UserSettingsManager::applyAutoDetectedPreset( const HardwareProbeResult& probe )
    {
        const hashed_string presetName = detectScalabilityPreset( probe );
        if ( presetName.empty() )
            return false;
        const ScalabilityGroupDef& group      = _schema.getScalabilityGroups().front();
        const uint32               groupIndex = findIndex( group._settingId );
        if ( groupIndex == invalid_index::kUint32 )
            return false;
        _listState[groupIndex]._committedValue = presetName.c_str();
        applyScalabilityPreset( group, presetName, true );
        SW_LOG_INFO( "Graphics preset '%#' picked for %# logical cores and %# MB memory", presetName.c_str(), probe._logicalCoreCount, probe._systemMemoryMb );
        return true;
    }

    void UserSettingsManager::reapplyAll()
    {
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            SettingState& state = _listState[settingIndex];
            // 다음 실행에 닿는 값은 기동 때 한 번만 바뀐다 — 게임을 다시 세울 때(핫 리로드)의 다시 적용은 기동 때 넣은 값을 지킨다.
            const bool bKeepStartupValue = _bStartupApplied && listSetting[settingIndex]._applyTiming == UserSettingApplyTiming::NeedsRestart;
            if ( bKeepStartupValue == false )
                state._startupValue = state._committedValue;
            (void)applyToTarget( settingIndex, state._startupValue, true ); // 대상이 없는 설정(이 실행에 없는 변수 · 적용기)은 건너뛴다 — 값은 저장된 그대로다
        }
        _bStartupApplied = true;
    }

    // ------------------------------------------------------------------------------
    // 4) 메뉴 UI 바인딩
    // ------------------------------------------------------------------------------
    void UserSettingsManager::collectSettings( const hashed_string& categoryId, vector<const UserSettingDef*>& outListSetting ) const
    {
        outListSetting.clear();
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( listSetting.size() ); ++settingIndex )
        {
            if ( listSetting[settingIndex]._category == categoryId && isAvailableAt( settingIndex ) )
                outListSetting.push_back( &listSetting[settingIndex] );
        }
    }

    string_view UserSettingsManager::getValue( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        return settingIndex != invalid_index::kUint32 ? string_view( getEffectiveValue( settingIndex ) ) : string_view{};
    }

    string_view UserSettingsManager::getAppliedValue( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        return settingIndex != invalid_index::kUint32 ? string_view( _listState[settingIndex]._committedValue ) : string_view{};
    }

    string_view UserSettingsManager::getDefaultValue( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        return settingIndex != invalid_index::kUint32 ? string_view( _listState[settingIndex]._defaultValue ) : string_view{};
    }

    bool UserSettingsManager::getBoolValue( const hashed_string& settingId ) const
    {
        return StringUtil::parseBool( getValue( settingId ), false );
    }

    int32 UserSettingsManager::getIntValue( const hashed_string& settingId ) const
    {
        int32 value{ 0 };
        return StringUtil::parseInt( getValue( settingId ), value ) ? value : 0;
    }

    float32 UserSettingsManager::getFloatValue( const hashed_string& settingId ) const
    {
        float32 value{ 0.0f };
        return StringUtil::parseFloat( getValue( settingId ), value ) ? value : 0.0f;
    }

    void UserSettingsManager::collectOptions( const hashed_string& settingId, vector<UserSettingOption>& outListOption ) const
    {
        outListOption.clear();
        const UserSettingDef* pDef = findSetting( settingId );
        if ( pDef == nullptr )
            return;
        if ( pDef->_optionProvider.empty() )
            outListOption = pDef->_listOption;
        else
            (void)_registry.collectOptions( pDef->_optionProvider, outListOption );
    }

    UserSettingSetResult UserSettingsManager::setPendingValue( const hashed_string& settingId, string_view value )
    {
        const uint32 settingIndex = findIndex( settingId );
        if ( settingIndex == invalid_index::kUint32 )
            return UserSettingSetResult::Unknown;
        if ( isEnabledAt( settingIndex ) == false )
            return UserSettingSetResult::Disabled;

        const UserSettingDef&        def = _schema.getSettings()[settingIndex];
        string                       normalized;
        const UserSettingValueResult normalizeResult = UserSettingsSchema::normalizeValue( def, value, normalized );
        if ( normalizeResult == UserSettingValueResult::Rejected )
            return UserSettingSetResult::Rejected;

        // 공급자 열거형은 지금 공급자가 주는 선택지와 대조한다(공급자가 아직 비었으면 — 언어 팩을 읽기 전 — 형식만 본다).
        if ( def._type == UserSettingType::Enum && def._optionProvider.empty() == false && normalized.empty() == false )
        {
            vector<UserSettingOption> listOption;
            (void)_registry.collectOptions( def._optionProvider, listOption );
            bool bFound = listOption.empty();
            for ( const UserSettingOption& option : listOption )
            {
                if ( StringUtil::equals( option._value, normalized, true ) )
                {
                    normalized = option._value;
                    bFound     = true;
                }
            }
            if ( bFound == false )
                return UserSettingSetResult::Rejected;
        }
        if ( def._type == UserSettingType::KeyBinding )
        {
            UserSettingBindingConflict conflict;
            if ( findBindingConflict( settingId, normalized, conflict ) )
                return UserSettingSetResult::Conflict;
        }

        const UserSettingSetResult result = setPendingAt( settingIndex, normalized, normalizeResult );
        if ( result == UserSettingSetResult::Unchanged )
            return result;

        // 품질 묶음: 프리셋을 고르면 묶인 설정이 따라가고, 묶인 설정을 바꾸면 묶음이 프리셋 이름이나 Custom 이 된다.
        const ScalabilityGroupDef* pGroup = _schema.findScalabilityGroupOf( settingId );
        if ( pGroup != nullptr && pGroup->_settingId == settingId )
            applyScalabilityPreset( *pGroup, hashed_string( normalized ), false );
        else if ( pGroup != nullptr )
            refreshScalabilityGroup( *pGroup, false );
        return result;
    }

    UserSettingSetResult UserSettingsManager::setPendingBoolValue( const hashed_string& settingId, bool bValue )
    {
        return setPendingValue( settingId, bValue ? "true" : "false" );
    }

    UserSettingSetResult UserSettingsManager::setPendingIntValue( const hashed_string& settingId, int32 value )
    {
        utf8 arrBuffer[constant::kMaxBuffer32]{};
        StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value );
        return setPendingValue( settingId, arrBuffer );
    }

    UserSettingSetResult UserSettingsManager::setPendingFloatValue( const hashed_string& settingId, float32 value )
    {
        utf8 arrBuffer[constant::kMaxBuffer64]{};
        StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, value );
        return setPendingValue( settingId, arrBuffer );
    }

    bool UserSettingsManager::isPending( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        return settingIndex != invalid_index::kUint32 && _listState[settingIndex]._bPending;
    }

    bool UserSettingsManager::hasPendingChanges() const
    {
        for ( const SettingState& state : _listState )
        {
            if ( state._bPending )
                return true;
        }
        return false;
    }

    bool UserSettingsManager::isSettingEnabled( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        return settingIndex != invalid_index::kUint32 && isEnabledAt( settingIndex );
    }

    bool UserSettingsManager::isSettingAvailable( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        return settingIndex != invalid_index::kUint32 && isAvailableAt( settingIndex );
    }

    // ------------------------------------------------------------------------------
    // 5) 적용 · 되돌리기 · 확인
    // ------------------------------------------------------------------------------
    UserSettingsApplyResult UserSettingsManager::applyPending()
    {
        UserSettingsApplyResult       result;
        const vector<UserSettingDef>& listSetting    = _schema.getSettings();
        float32                       confirmSeconds = 0.0f;
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            SettingState& state = _listState[settingIndex];
            if ( state._bPending == false )
                continue;
            const UserSettingDef& def      = listSetting[settingIndex];
            const bool            bChanged = state._pendingValue != state._committedValue;
            if ( bChanged && def._confirmSeconds > 0.0f )
            {
                // 이미 확인 대기 중이면 처음의 옛 값을 지킨다 — 두 번 바꾸고 시간이 다 되면 처음 화면으로 돌아가야 한다.
                if ( state._bRollback == false )
                {
                    state._rollbackValue = state._committedValue;
                    state._bRollback     = true;
                }
                confirmSeconds = confirmSeconds > def._confirmSeconds ? confirmSeconds : def._confirmSeconds;
            }
            state._committedValue = state._pendingValue;
            state._pendingValue.clear();
            state._bPending = false;

            if ( def._applyTiming == UserSettingApplyTiming::NeedsRestart )
            {
                result._bNeedsRestart = result._bNeedsRestart || state._committedValue != state._startupValue;
                continue;
            }
            // `Immediate` 는 보류 때 이미 넣었다. 다시 넣어도 같은 값이라 나머지와 한 길로 둔다(적용기는 같은 값을 다시 받아도 된다).
            if ( applyToTarget( settingIndex, state._committedValue, false ) )
                ++result._appliedCount;
            else
            {
                // 대상이 거절한 값은 저장은 되지만 지금 실행에는 닿지 않았다 — 어느 설정인지 남긴다(메뉴 · 에디터는 수만 받는다).
                ++result._failedCount;
                SW_LOG_WARNING( "User setting '%#' = '%#' was rejected by its target '%#'", def._id.c_str(), state._committedValue.c_str(), def._targetName.c_str() );
            }
            broadcast( def._id, UserSettingEventKind::Applied );
        }

        if ( confirmSeconds > 0.0f )
        {
            _confirmSecondsLeft      = confirmSeconds;
            result._bAwaitingConfirm = true;
            broadcast( {}, UserSettingEventKind::ConfirmStarted );
        }
        result._bSaved = saveIfPathSet();
        return result;
    }

    void UserSettingsManager::revertPending()
    {
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            SettingState& state = _listState[settingIndex];
            if ( state._bPending == false )
                continue;
            const bool bPreviewed = listSetting[settingIndex]._applyTiming == UserSettingApplyTiming::Immediate && state._pendingValue != state._committedValue;
            state._pendingValue.clear();
            state._bPending = false;
            if ( bPreviewed )
                (void)applyToTarget( settingIndex, state._committedValue, false ); // 미리 보기를 되돌린다 — 대상이 없는 설정은 건너뛴다
            broadcast( listSetting[settingIndex]._id, UserSettingEventKind::Reverted );
        }
    }

    void UserSettingsManager::resetCategoryToDefaults( const hashed_string& categoryId )
    {
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            if ( listSetting[settingIndex]._category != categoryId || isAvailableAt( settingIndex ) == false )
                continue;
            // 키 바인딩은 겹침 검사를 건너뛴다 — 모두 기본으로 돌아가면 기본 맵끼리는 겹치지 않는다.
            (void)setPendingAt( settingIndex, _listState[settingIndex]._defaultValue, UserSettingValueResult::Accepted );
        }
        for ( const ScalabilityGroupDef& group : _schema.getScalabilityGroups() )
        {
            refreshScalabilityGroup( group, false );
        }
    }

    void UserSettingsManager::confirmChanges()
    {
        if ( _confirmSecondsLeft <= 0.0f )
            return;
        _confirmSecondsLeft = 0.0f;
        for ( SettingState& state : _listState )
        {
            state._bRollback = false;
            state._rollbackValue.clear();
        }
        (void)saveIfPathSet(); // 경로가 없으면 저장하지 않는 실행이고, 쓰기 실패는 saveUserFile 이 경고로 남긴다
        broadcast( {}, UserSettingEventKind::Confirmed );
    }

    void UserSettingsManager::update( float32 deltaSeconds )
    {
        if ( _confirmSecondsLeft <= 0.0f )
            return;
        _confirmSecondsLeft -= deltaSeconds;
        if ( _confirmSecondsLeft > 0.0f )
            return;

        _confirmSecondsLeft = 0.0f;
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            SettingState& state = _listState[settingIndex];
            if ( state._bRollback == false )
                continue;
            state._committedValue = state._rollbackValue;
            state._rollbackValue.clear();
            state._bRollback = false;
            (void)applyToTarget( settingIndex, state._committedValue, false ); // 확인 전 값으로 되돌린다 — 대상이 없는 설정은 건너뛴다
        }
        SW_LOG_INFO( "Display change was not confirmed - reverted" );
        (void)saveIfPathSet(); // 경로가 없으면 저장하지 않는 실행이고, 쓰기 실패는 saveUserFile 이 경고로 남긴다
        broadcast( {}, UserSettingEventKind::ConfirmTimedOut );
    }

    bool UserSettingsManager::isRestartRequired() const
    {
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( size_t settingIndex = 0; settingIndex < _listState.size(); ++settingIndex )
        {
            const bool bRestartSetting = listSetting[settingIndex]._applyTiming == UserSettingApplyTiming::NeedsRestart;
            if ( bRestartSetting && _listState[settingIndex]._committedValue != _listState[settingIndex]._startupValue )
                return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------------
    // 6) 키 바인딩
    // ------------------------------------------------------------------------------
    bool UserSettingsManager::findBindingConflict( const hashed_string& settingId, string_view slotText, UserSettingBindingConflict& outConflict ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        if ( settingIndex == invalid_index::kUint32 )
            return false;
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        const UserSettingDef&         def         = listSetting[settingIndex];
        if ( def._type != UserSettingType::KeyBinding )
            return false;

        InputSlot  slot;
        InputMap*  pInputMap = getTargets()._pInputMap;
        const bool bHasSlot  = slotText.empty() ? ( pInputMap != nullptr && pInputMap->findDefaultRebindSlot( def._action, def._bindIndex, slot ) )
                                                : InputSlotUtil::tryParse( slotText, slot );
        if ( bHasSlot == false )
            return false;

        const hashed_string context = findBindingContext( settingIndex );
        for ( uint32 otherIndex = 0; otherIndex < static_cast<uint32>( listSetting.size() ); ++otherIndex )
        {
            const UserSettingDef& other = listSetting[otherIndex];
            if ( otherIndex == settingIndex || other._type != UserSettingType::KeyBinding )
                continue;
            InputSlot otherSlot;
            if ( findBindingContext( otherIndex ) != context || findEffectiveSlot( otherIndex, otherSlot ) == false )
                continue;
            if ( otherSlot == slot )
            {
                outConflict._settingId = other._id;
                outConflict._action    = other._action;
                return true;
            }
        }

        // 스키마가 다루지 않는 액션(메뉴에 없는 바인딩)과도 겹치면 안 된다. 스키마가 다루는 액션은 위에서 보류 값으로 봤다.
        string conflictingAction;
        if ( pInputMap == nullptr || pInputMap->hasBindingConflict( slot, context, conflictingAction ) == false )
            return false;
        const hashed_string conflictingName( conflictingAction );
        if ( conflictingName == def._action )
            return false;
        for ( const UserSettingDef& other : listSetting )
        {
            if ( other._type == UserSettingType::KeyBinding && other._action == conflictingName )
                return false;
        }
        outConflict._settingId = {};
        outConflict._action    = conflictingName;
        return true;
    }

    UserSettingSetResult UserSettingsManager::setPendingBinding( const hashed_string& settingId, string_view slotText, UserSettingBindingPolicy policy )
    {
        const uint32 settingIndex = findIndex( settingId );
        if ( settingIndex == invalid_index::kUint32 )
            return UserSettingSetResult::Unknown;
        if ( _schema.getSettings()[settingIndex]._type != UserSettingType::KeyBinding )
            return UserSettingSetResult::Rejected;

        UserSettingBindingConflict conflict;
        if ( findBindingConflict( settingId, slotText, conflict ) == false )
            return setPendingValue( settingId, slotText );
        if ( policy == UserSettingBindingPolicy::Reject || conflict._settingId.empty() )
            return UserSettingSetResult::Conflict;

        // 맞바꾸기: 겹친 설정이 이 설정의 지금 키를 받는다. 기본 키(빈 값)면 입력 맵에서 실제 키를 읽어 적는다.
        InputSlot currentSlot;
        string    currentText;
        if ( findEffectiveSlot( settingIndex, currentSlot ) )
            currentText = InputSlotUtil::toText( currentSlot );
        string normalized;
        if ( UserSettingsSchema::normalizeValue( _schema.getSettings()[settingIndex], slotText, normalized ) == UserSettingValueResult::Rejected )
            return UserSettingSetResult::Rejected;
        (void)setPendingAt( findIndex( conflict._settingId ), currentText, UserSettingValueResult::Accepted );
        return setPendingAt( settingIndex, normalized, UserSettingValueResult::Accepted );
    }

    string UserSettingsManager::getBindingGlyph( const hashed_string& settingId, InputGlyphStyle style ) const
    {
        const uint32 settingIndex = findIndex( settingId );
        InputSlot    slot;
        if ( settingIndex == invalid_index::kUint32 || findEffectiveSlot( settingIndex, slot ) == false )
            return "[ ? ]";
        return InputMap::getGlyphForSlot( slot, style );
    }

    // ------------------------------------------------------------------------------
    // 7) 통보 · 모듈
    // ------------------------------------------------------------------------------
    DelegateHandle UserSettingsManager::registerEventListener( const UserSettingEventListener& listener )
    {
        return _onEvent.add( listener );
    }

    void UserSettingsManager::unregisterEventListener( const DelegateHandle& handle )
    {
        _onEvent.remove( handle );
    }

    uint32 UserSettingsManager::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        return _onEvent.removeCodeWithin( pBegin, pEnd ) + _registry.removeCodeWithin( pBegin, pEnd );
    }

    // ------------------------------------------------------------------------------
    // 내부
    // ------------------------------------------------------------------------------
    uint32 UserSettingsManager::findIndex( const hashed_string& settingId ) const
    {
        const uint32 settingIndex = _schema.findSettingIndex( settingId );
        return settingIndex < static_cast<uint32>( _listState.size() ) ? settingIndex : invalid_index::kUint32;
    }

    const string& UserSettingsManager::getEffectiveValue( uint32 settingIndex ) const
    {
        const SettingState& state = _listState[settingIndex];
        return state._bPending ? state._pendingValue : state._committedValue;
    }

    bool UserSettingsManager::isAvailableAt( uint32 settingIndex ) const
    {
        return ( _schema.getSettings()[settingIndex]._platformMask & UserSettingsPlatform::getCurrent() ) != 0;
    }

    bool UserSettingsManager::isEnabledAt( uint32 settingIndex ) const
    {
        if ( isAvailableAt( settingIndex ) == false )
            return false;
        for ( const UserSettingCondition& condition : _schema.getSettings()[settingIndex]._listEnabledCondition )
        {
            const uint32 otherIndex = findIndex( condition._settingId );
            if ( otherIndex == invalid_index::kUint32 )
                return false;
            const bool bEqual     = StringUtil::equals( getEffectiveValue( otherIndex ), condition._value, true );
            const bool bSatisfied = condition._operator == UserSettingConditionOperator::Equal ? bEqual : bEqual == false;
            if ( bSatisfied == false )
                return false;
        }
        return true;
    }

    bool UserSettingsManager::applyToTarget( uint32 settingIndex, const string& value, bool bStartup )
    {
        if ( isAvailableAt( settingIndex ) == false )
            return true;
        const UserSettingDef& def = _schema.getSettings()[settingIndex];
        switch ( def._targetKind )
        {
            case UserSettingTargetKind::None:
            {
                return true;
            }
            case UserSettingTargetKind::GlobalVariable:
            {
                GlobalVariableManager* pGlobalVariableManager = getTargets()._pGlobalVariableManager;
                GlobalVariableInfo*    pVariable              = pGlobalVariableManager != nullptr ? pGlobalVariableManager->findVariable( def._targetName.c_str() ) : nullptr;
                if ( pVariable == nullptr )
                    return false;
                // 기동 · 다시 세울 때의 적용은 명령줄로 준 변수를 덮지 않는다 — 실행 한 번의 값(명령줄)이 저장된 값(사용자 설정)을 이긴다(화면 설정과 같은 순서).
                // 메뉴에서 바꾼 값(bStartup 거짓)은 덮는다 — 플레이어가 지금 고른 것이다.
                const CommandLineManager* pCommandLineManager = getTargets()._pCommandLineManager;
                string                    commandLineValue;
                if ( bStartup && pCommandLineManager != nullptr && pCommandLineManager->findPendingGlobalValue( def._targetName.view(), commandLineValue ) )
                    return true;
                if ( def._type != UserSettingType::Enum )
                    return pVariable->setValueFromString( value );

                // 열거형은 선택지의 `targetValue`, 없으면 선택지 순번을 넘긴다. 문자열 변수면 선택지 이름 그대로다.
                const uint32 optionIndex = UserSettingsManagerInternal::findOptionIndex( def, value );
                if ( pVariable->_type == GlobalVariableType::String || optionIndex == invalid_index::kUint32 )
                    return pVariable->setValueFromString( value );
                const UserSettingOption& option = def._listOption[optionIndex];
                if ( option._targetValue.empty() == false )
                    return pVariable->setValueFromString( option._targetValue );
                return pVariable->setValueAsInt( static_cast<int32>( optionIndex ) );
            }
            case UserSettingTargetKind::Applier:
            {
                UserSettingApplyContext context;
                context._pDef     = &def;
                context._value    = value;
                context._param    = def._targetParam;
                context._bStartup = bStartup;
                context._bDefault = value == _listState[settingIndex]._defaultValue;
                return _registry.invokeApplier( def._targetName, context );
            }
        }
        return false;
    }

    UserSettingSetResult UserSettingsManager::setPendingAt( uint32 settingIndex, const string& normalized, UserSettingValueResult normalizeResult )
    {
        SettingState& state = _listState[settingIndex];
        if ( getEffectiveValue( settingIndex ) == normalized )
            return UserSettingSetResult::Unchanged;

        const UserSettingDef& def = _schema.getSettings()[settingIndex];
        if ( normalized == state._committedValue )
        {
            // 확정 값으로 되돌아왔으면 보류가 아니다(메뉴의 "바뀐 것 있음" 표시가 꺼진다).
            state._pendingValue.clear();
            state._bPending = false;
        }
        else
        {
            state._pendingValue = normalized;
            state._bPending     = true;
        }
        if ( def._applyTiming == UserSettingApplyTiming::Immediate )
            (void)applyToTarget( settingIndex, normalized, false ); // 즉시 미리 보기 — 대상이 없는 설정은 건너뛰고, 확정(commit)이 결과를 다시 본다
        broadcast( def._id, UserSettingEventKind::PendingChanged );
        return normalizeResult == UserSettingValueResult::Clamped ? UserSettingSetResult::Clamped : UserSettingSetResult::Accepted;
    }

    void UserSettingsManager::refreshScalabilityGroup( const ScalabilityGroupDef& group, bool bCommitted )
    {
        const uint32 groupIndex = findIndex( group._settingId );
        if ( groupIndex == invalid_index::kUint32 )
            return;

        string matchedPreset = group._customValue;
        for ( const ScalabilityPresetDef& preset : group._listPreset )
        {
            bool bMatches = true;
            for ( const ScalabilityPresetValue& value : preset._listValue )
            {
                const uint32 memberIndex = findIndex( value._settingId );
                string       normalized;
                if ( memberIndex == invalid_index::kUint32 ||
                     UserSettingsSchema::normalizeValue( _schema.getSettings()[memberIndex], value._value, normalized ) == UserSettingValueResult::Rejected )
                {
                    bMatches = false;
                    break;
                }
                const string& current = bCommitted ? _listState[memberIndex]._committedValue : getEffectiveValue( memberIndex );
                if ( current != normalized )
                {
                    bMatches = false;
                    break;
                }
            }
            if ( bMatches )
            {
                matchedPreset = preset._name.c_str();
                break;
            }
        }

        if ( bCommitted )
            _listState[groupIndex]._committedValue = matchedPreset;
        else
            (void)setPendingAt( groupIndex, matchedPreset, UserSettingValueResult::Accepted );
    }

    void UserSettingsManager::applyScalabilityPreset( const ScalabilityGroupDef& group, const hashed_string& presetName, bool bCommitted )
    {
        for ( const ScalabilityPresetDef& preset : group._listPreset )
        {
            if ( preset._name != presetName )
                continue;
            for ( const ScalabilityPresetValue& value : preset._listValue )
            {
                const uint32 memberIndex = findIndex( value._settingId );
                string       normalized;
                if ( memberIndex == invalid_index::kUint32 ||
                     UserSettingsSchema::normalizeValue( _schema.getSettings()[memberIndex], value._value, normalized ) == UserSettingValueResult::Rejected )
                    continue;
                if ( bCommitted )
                    _listState[memberIndex]._committedValue = normalized;
                else
                    (void)setPendingAt( memberIndex, normalized, UserSettingValueResult::Accepted );
            }
            return;
        }
        // "custom" 은 프리셋이 없다 — 묶인 설정은 그대로 둔다.
    }

    bool UserSettingsManager::findEffectiveSlot( uint32 settingIndex, InputSlot& outSlot ) const
    {
        const UserSettingDef& def   = _schema.getSettings()[settingIndex];
        const string&         value = getEffectiveValue( settingIndex );
        if ( value.empty() == false )
            return InputSlotUtil::tryParse( value, outSlot );
        const InputMap* pInputMap = getTargets()._pInputMap;
        return pInputMap != nullptr && pInputMap->findDefaultRebindSlot( def._action, def._bindIndex, outSlot );
    }

    hashed_string UserSettingsManager::findBindingContext( uint32 settingIndex ) const
    {
        const UserSettingDef& def = _schema.getSettings()[settingIndex];
        if ( def._context.empty() == false )
            return def._context;
        const InputMap* pInputMap = getTargets()._pInputMap;
        if ( pInputMap == nullptr )
            return {};
        const hashed_string layer = pInputMap->findBindingLayer( def._action, def._bindIndex );
        return layer.empty() ? pInputMap->getDefaultLayerName() : layer;
    }

    void UserSettingsManager::broadcast( const hashed_string& settingId, UserSettingEventKind kind )
    {
        UserSettingEvent event;
        event._settingId = settingId;
        event._kind      = kind;
        _onEvent.broadcast( event );
    }

    bool UserSettingsManager::saveIfPathSet()
    {
        if ( _userFilePath.empty() )
            return false;
        return saveUserFile( _userFilePath );
    }
} // namespace sw
