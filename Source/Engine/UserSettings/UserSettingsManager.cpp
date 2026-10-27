#include "pch.h"

#include "Engine/UserSettings/UserSettingsManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/PackEngineSettingsSubsystem.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsFile.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettings" );

    namespace
    {
        /** @brief 열거형 선택지의 순번입니다(전역 변수에 넘길 기본 글). */
        uint32 findOptionIndex( const UserSettingDef& def, string_view value )
        {
            for ( uint32 optionIndex = 0; optionIndex < static_cast<uint32>( def._listOption.size() ); ++optionIndex )
            {
                if ( StringUtil::equals( def._listOption[optionIndex]._value, value ) )
                    return optionIndex;
            }
            return invalid_index::kUint32;
        }
    } // namespace
} // namespace sw

namespace sw
{
    UserSettingsManager::UserSettingsManager()
        : _schema{}
        , _registry{}
        , _engineHandlers{}
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
        _engineHandlers.initialize( targets, _registry );
    }

    void UserSettingsManager::shutdown()
    {
        _engineHandlers.shutdown();
        _registry.clear();
        _schema.clear();
        _listState.clear();
        _onEvent.clear();
        _userFilePath.clear();
        _confirmSecondsLeft = 0.0f;
        _bUserFileLoaded    = false;
        _bStartupApplied    = false;
    }

    Subscription UserSettingsManager::addKeyBindingInputMap( InputMap& inputMap )
    {
        if ( _engineHandlers.addBindingInputMap( inputMap ) == false )
            return {}; // 이미 더했다 — 먼저 받은 구독이 쥔다
        // 이 맵의 액션을 고치는 설정은 맵이 없던 기동 적용에서 건너뛰었다 — 지금 확정 값을 넣는다.
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            const UserSettingDef& def = listSetting[settingIndex];
            if ( def._type == UserSettingType::KeyBinding && inputMap.hasAction( def._action ) )
                (void)applyToTarget( settingIndex, _listState[settingIndex]._committedValue, true ); // 못 넣은 키는 핸들러가 경고한다 — 값은 저장된 그대로다
        }
        return issueSubscription( reinterpret_cast<uint64>( &inputMap ) );
    }

    void UserSettingsManager::releaseSubscription( uint64 id )
    {
        _engineHandlers.removeBindingInputMap( *reinterpret_cast<const InputMap*>( id ) ); // 주소로만 찾는다 — 역참조하지 않는다
    }

    // ------------------------------------------------------------------------------
    // 1) 스키마
    // ------------------------------------------------------------------------------
    bool UserSettingsManager::loadSchema( const ResourceID& resourceID )
    {
        const UserSettingsSchema backup  = _schema;
        const bool               bLoaded = _schema.loadFromResource( resourceID );
        if ( appendSchema( bLoaded ) )
            return true;
        _schema = backup;
        return false;
    }

    bool UserSettingsManager::loadSchemaFromXMLText( string_view xmlText, string_view sourceName )
    {
        const UserSettingsSchema backup  = _schema;
        const bool               bLoaded = _schema.loadFromXMLText( xmlText, sourceName );
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

        // 품질 번들의 기본 프리셋이 묶인 설정의 기본값이다. 두 곳에 적은 기본값이 어긋나 첫 화면이 Custom 이 되는 일이 없게 한다.
        for ( const ScalabilityGroupDef& group : _schema.getScalabilityGroups() )
        {
            const uint32 groupIndex = findIndex( group._settingID );
            if ( groupIndex == invalid_index::kUint32 )
                continue;
            const hashed_string presetName( _listState[groupIndex]._defaultValue );
            for ( const ScalabilityPresetDef& preset : group._listPreset )
            {
                if ( preset._name != presetName )
                    continue;
                for ( const ScalabilityPresetValue& value : preset._listValue )
                {
                    const uint32 memberIndex = findIndex( value._settingID );
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

    bool UserSettingsManager::setGameDefault( const hashed_string& settingID, string_view value )
    {
        const uint32 settingIndex = findIndex( settingID );
        if ( settingIndex == invalid_index::kUint32 )
        {
            SW_LOG_ERROR( "game default for an unknown setting '%#'", settingID.c_str() );
            return false;
        }
        string normalized;
        if ( UserSettingsSchema::normalizeValue( _schema.getSettings()[settingIndex], value, normalized ) != UserSettingValueResult::Accepted )
        {
            SW_LOG_ERROR( "game default '%#' for '%#' is not a valid value", value, settingID.c_str() );
            return false;
        }
        SettingState& state = _listState[settingIndex];
        if ( state._committedValue == state._defaultValue )
            state._committedValue = normalized;
        state._defaultValue = normalized;

        const ScalabilityGroupDef* pGroup = _schema.findScalabilityGroupOf( settingID );
        if ( pGroup != nullptr && pGroup->_settingID == settingID )
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
        // 사용자 파일은 엔진 필수 데이터와 달리 복구가 필요하다. 깨진 파일은 조용히 기본값으로 덮지 않고 `.corrupt` 로 치운 뒤
        // 오류를 남긴다 — 다음 저장이 새 파일을 만들고, 사용자는 치운 파일로 무엇이 있었는지 볼 수 있다.
        string text;
        if ( FileUtil::readTextFile( filePath, text ) && loadUserJSON( text, filePath ) )
            return true;
        const string corruptPath = string( filePath ) + ".corrupt";
        const bool   bMovedAside = FileUtil::copyFile( filePath, corruptPath ) && FileUtil::removeFile( filePath );
        SW_LOG_ERROR_ID( "UserSettings.FileUnreadable", "User settings '%#' could not be read - %# and starting from a fresh file", filePath,
                         bMovedAside ? ( "moved to '" + corruptPath + "'" ).c_str() : "could not be moved aside" );
        return false;
    }

    bool UserSettingsManager::loadUserJSON( string_view jsonText, string_view sourceName )
    {
        vector<UserSettingsFileEntry> listEntry;
        if ( UserSettingsFile::parseJSON( jsonText, sourceName, _schema, listEntry ) == false )
            return false;

        for ( SettingState& state : _listState )
        {
            state._committedValue = state._defaultValue;
            state._pendingValue.clear();
            state._bPending  = false;
            state._bRollback = false;
        }
        for ( const UserSettingsFileEntry& entry : listEntry )
        {
            const uint32 settingIndex = findIndex( hashed_string( entry._key ) );
            if ( settingIndex == invalid_index::kUint32 )
            {
                SW_LOG_WARNING_ID( "UserSettings.UnknownKeyDropped", "User settings '%#': dropped unknown key '%#'", sourceName, entry._key );
                continue;
            }
            string                       normalized;
            const UserSettingValueResult result = UserSettingsSchema::normalizeValue( _schema.getSettings()[settingIndex], entry._value, normalized );
            if ( result == UserSettingValueResult::Rejected )
            {
                SW_LOG_WARNING_ID( "UserSettings.ValueInvalid", "User settings '%#': '%#' has an invalid value '%#' - using the default", sourceName, entry._key, entry._value );
                continue;
            }
            if ( result == UserSettingValueResult::Clamped )
                SW_LOG_WARNING_ID( "UserSettings.ValueOutOfRange", "User settings '%#': '%#' = '%#' is out of range - using '%#'", sourceName, entry._key, entry._value, normalized );
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

    string UserSettingsManager::makeUserJSON() const
    {
        const vector<UserSettingDef>&  listSetting = _schema.getSettings();
        vector<UserSettingsWriteEntry> listEntry;
        for ( size_t settingIndex = 0; settingIndex < _listState.size(); ++settingIndex )
        {
            const SettingState& state = _listState[settingIndex];
            // 확인 대기 중이면 기존 값을 쓴다. 확인 전에 꺼지면 다음 실행은 확인된 화면으로 뜬다.
            const string& value = state._bRollback ? state._rollbackValue : state._committedValue;
            if ( value == state._defaultValue )
                continue;
            listEntry.push_back( UserSettingsWriteEntry{ listSetting[settingIndex], value } );
        }
        return UserSettingsFile::makeJSON( _schema, listEntry );
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
        // 교체 재시도 sleep 이 이 안에 있다(`FileUtil::writeAtomically`, Core 라 프로파일러를 못 쓴다). 서비스가 묶이기 전 호출은 계측 없이 쓴다.
        const bool bWritten = [&]()
        {
            if ( engine::areEngineServicesBound() == false )
                return FileUtil::writeTextFile( filePath, makeUserJSON() );
            SW_PROFILE_SCOPE( "GT.Settings.save" );
            return FileUtil::writeTextFile( filePath, makeUserJSON() );
        }();
        if ( bWritten == false )
        {
            SW_LOG_WARNING( "Failed to write user settings '%#'", filePath );
            return false;
        }
        return true;
    }

    string UserSettingsManager::makeDefaultUserFilePath( string_view gameName )
    {
        return FileUtil::joinPath( UserDataPath::getConfigDirectory( gameName ), UserSettingsFile::kFileName );
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
            const bool bMatches = probe._logicalCoreCount >= rule._minLogicalCoreCount && probe._systemMemoryMb >= rule._minSystemMemoryMb && probe._gpuMemoryMb >= rule._minGPUMemoryMb;
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
        const uint32               groupIndex = findIndex( group._settingID );
        if ( groupIndex == invalid_index::kUint32 )
            return false;
        _listState[groupIndex]._committedValue = presetName.c_str();
        applyScalabilityPreset( group, presetName, true );
        SW_LOG_INFO( "Graphics preset '%#' picked for %# logical cores, %# MB memory and %# MB video memory (%#)", presetName.c_str(), probe._logicalCoreCount,
                     probe._systemMemoryMb, probe._gpuMemoryMb, probe._gpuName.empty() ? "unknown GPU" : probe._gpuName.c_str() );
        return true;
    }

    void UserSettingsManager::reapplyAll()
    {
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            SettingState& state = _listState[settingIndex];
            // 다음 실행에 반영되는 값은 기동 때 한 번만 바뀐다. 게임을 다시 만들 때(핫 리로드)의 다시 적용은 기동 때 넣은 값을 지킨다.
            const bool bKeepStartupValue = _bStartupApplied && listSetting[settingIndex]._applyTiming == UserSettingApplyTiming::NeedsRestart;
            if ( bKeepStartupValue == false )
                state._startupValue = state._committedValue;
            (void)applyToTarget( settingIndex, state._startupValue, true ); // 대상이 없는 설정(이 실행에 없는 변수나 핸들러)은 건너뛴다. 값은 저장된 그대로다
        }
        _bStartupApplied = true;
    }

    // ------------------------------------------------------------------------------
    // 4) 메뉴 UI 바인딩
    // ------------------------------------------------------------------------------
    void UserSettingsManager::collectSettings( const hashed_string& categoryID, vector<const UserSettingDef*>& outListSetting ) const
    {
        outListSetting.clear();
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( listSetting.size() ); ++settingIndex )
        {
            if ( listSetting[settingIndex]._category == categoryID && isAvailableAt( settingIndex ) )
                outListSetting.push_back( &listSetting[settingIndex] );
        }
    }

    string_view UserSettingsManager::getValue( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        return settingIndex != invalid_index::kUint32 ? string_view( getEffectiveValue( settingIndex ) ) : string_view{};
    }

    string_view UserSettingsManager::getAppliedValue( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        return settingIndex != invalid_index::kUint32 ? string_view( _listState[settingIndex]._committedValue ) : string_view{};
    }

    string_view UserSettingsManager::getDefaultValue( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        return settingIndex != invalid_index::kUint32 ? string_view( _listState[settingIndex]._defaultValue ) : string_view{};
    }

    bool UserSettingsManager::getBoolValue( const UserSettingKey<bool>& key ) const
    {
        bool                        bValue{ false };
        [[maybe_unused]] const bool bParsed = StringUtil::tryParseBool( getKeyValue( key._settingID, key._settingIndex ), bValue );
        SW_ASSERT( bParsed ); // 값은 `normalizeValue` 를 거쳐 들어왔다 — 실패는 정규화의 결함이다
        return bValue;
    }

    int32 UserSettingsManager::getIntValue( const UserSettingKey<int32>& key ) const
    {
        int32                       value{ 0 };
        [[maybe_unused]] const bool bParsed = StringUtil::parseInt( getKeyValue( key._settingID, key._settingIndex ), value );
        SW_ASSERT( bParsed ); // 값은 `normalizeValue` 가 범위 안의 정수로 맞춰 넣었다
        return value;
    }

    float32 UserSettingsManager::getFloatValue( const UserSettingKey<float32>& key ) const
    {
        float32                     value{ 0.0f };
        [[maybe_unused]] const bool bParsed = StringUtil::parseFloat( getKeyValue( key._settingID, key._settingIndex ), value );
        SW_ASSERT( bParsed ); // 값은 `normalizeValue` 가 수로 맞춰 넣었다
        return value;
    }

    void UserSettingsManager::collectOptions( const hashed_string& settingID, vector<UserSettingOption>& outListOption ) const
    {
        outListOption.clear();
        const UserSettingDef* pDef = findSetting( settingID );
        if ( pDef == nullptr )
            return;
        collectOptionsOf( *pDef, outListOption );
    }

    void UserSettingsManager::collectOptionsOf( const UserSettingDef& def, vector<UserSettingOption>& outListOption ) const
    {
        outListOption.clear();
        if ( def._optionProvider.empty() )
        {
            outListOption = def._listOption;
            return;
        }
        (void)_registry.collectOptions( def._optionProvider, outListOption ); // 공급자가 없거나 비면 아래 고정 선택지가 채운다
        if ( def._listOption.empty() )
            return;
        if ( outListOption.empty() )
        {
            outListOption = def._listOption;
            return;
        }
        const uint32 settingIndex = findIndex( def._id );
        if ( settingIndex == invalid_index::kUint32 )
            return;
        const string* arrValue[] = { &_listState[settingIndex]._defaultValue, &getEffectiveValue( settingIndex ) };
        for ( const string* pValue : arrValue )
        {
            bool bListed = pValue->empty();
            for ( const UserSettingOption& option : outListOption )
            {
                bListed = bListed || StringUtil::equals( option._value, *pValue );
            }
            if ( bListed == false )
            {
                UserSettingOption option;
                option._value = *pValue;
                outListOption.push_back( option );
            }
        }
    }

    UserSettingSetResult UserSettingsManager::setPendingValue( const hashed_string& settingID, string_view value )
    {
        const uint32 settingIndex = findIndex( settingID );
        if ( settingIndex == invalid_index::kUint32 )
            return UserSettingSetResult::Unknown;
        if ( isEnabledAt( settingIndex ) == false )
            return UserSettingSetResult::Disabled;

        const UserSettingDef&        def = _schema.getSettings()[settingIndex];
        string                       normalized;
        const UserSettingValueResult normalizeResult = UserSettingsSchema::normalizeValue( def, value, normalized );
        if ( normalizeResult == UserSettingValueResult::Rejected )
            return UserSettingSetResult::Rejected;

        // 공급자 열거형은 지금 공급자가 주는 선택지와 대조한다(공급자가 아직 비었으면, 곧 언어 팩을 읽기 전이면 형식만 본다).
        const bool bCheckProviderOption = def._type == UserSettingType::Enum && def._optionProvider.empty() == false && normalized.empty() == false;
        if ( bCheckProviderOption )
        {
            vector<UserSettingOption> listOption;
            collectOptionsOf( def, listOption );
            bool bFound = listOption.empty();
            for ( const UserSettingOption& option : listOption )
            {
                if ( StringUtil::equals( option._value, normalized ) )
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
            if ( findBindingConflict( settingID, normalized, conflict ) )
                return UserSettingSetResult::Conflict;
        }

        const UserSettingSetResult result = setPendingAt( settingIndex, normalized, normalizeResult );
        if ( result == UserSettingSetResult::Unchanged )
            return result;

        // 품질 번들: 프리셋을 고르면 묶인 설정이 따라가고, 묶인 설정을 바꾸면 번들이 프리셋 이름이나 Custom 이 된다.
        const ScalabilityGroupDef* pGroup = _schema.findScalabilityGroupOf( settingID );
        if ( pGroup != nullptr && pGroup->_settingID == settingID )
            applyScalabilityPreset( *pGroup, hashed_string( normalized ), false );
        else if ( pGroup != nullptr )
            refreshScalabilityGroup( *pGroup, false );
        return result;
    }

    UserSettingSetResult UserSettingsManager::setPendingBoolValue( const hashed_string& settingID, bool bValue )
    {
        return setPendingValue( settingID, bValue ? "true" : "false" );
    }

    UserSettingSetResult UserSettingsManager::setPendingIntValue( const hashed_string& settingID, int32 value )
    {
        utf8 arrBuffer[constant::kMaxBuffer32]{};
        StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value );
        return setPendingValue( settingID, arrBuffer );
    }

    UserSettingSetResult UserSettingsManager::setPendingFloatValue( const hashed_string& settingID, float32 value )
    {
        utf8 arrBuffer[constant::kMaxBuffer64]{};
        StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, value );
        return setPendingValue( settingID, arrBuffer );
    }

    bool UserSettingsManager::isPending( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        return settingIndex != invalid_index::kUint32 && _listState[settingIndex]._bPending;
    }

    bool UserSettingsManager::hasPendingChanges() const
    {
        return VectorUtil::containsIf( _listState, [&]( const SettingState& state )
        { return state._bPending; } );
    }

    bool UserSettingsManager::isSettingEnabled( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        return settingIndex != invalid_index::kUint32 && isEnabledAt( settingIndex );
    }

    bool UserSettingsManager::isSettingAvailable( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        return settingIndex != invalid_index::kUint32 && isAvailableAt( settingIndex );
    }

    // ------------------------------------------------------------------------------
    // 5) 적용, 되돌리기, 확인
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
                // 이미 확인 대기 중이면 처음의 기존 값을 유지한다. 두 번 바꾸고 시간이 다 되면 처음 화면으로 돌아가야 한다.
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
            // `Immediate` 는 보류 때 이미 넣었다. 다시 넣어도 같은 값이라 나머지와 한 길로 둔다(핸들러는 같은 값을 다시 받아도 된다).
            if ( applyToTarget( settingIndex, state._committedValue, false ) )
                ++result._appliedCount;
            else
            {
                // 대상이 거절한 값은 저장은 되지만 지금 실행에는 반영되지 않았다. 어느 설정인지 남긴다(메뉴와 에디터는 개수만 받는다).
                ++result._failedCount;
                SW_LOG_WARNING_ID( "UserSettings.TargetRejected", "User setting '%#' = '%#' was rejected by its target '%#'", def._id.c_str(), state._committedValue.c_str(), def._targetName.c_str() );
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
                (void)applyToTarget( settingIndex, state._committedValue, false ); // 미리 보기를 되돌린다. 대상이 없는 설정은 건너뛴다
            broadcast( listSetting[settingIndex]._id, UserSettingEventKind::Reverted );
        }
    }

    void UserSettingsManager::resetCategoryToDefaults( const hashed_string& categoryID )
    {
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listState.size() ); ++settingIndex )
        {
            if ( listSetting[settingIndex]._category != categoryID || isAvailableAt( settingIndex ) == false )
                continue;
            // 키 바인딩은 겹침 검사를 건너뛴다. 모두 기본으로 돌아가면 기본 맵끼리는 겹치지 않는다.
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
        (void)saveIfPathSet(); // 경로가 없으면 저장하지 않는 실행이며, 쓰기 실패는 saveUserFile 이 경고로 남긴다
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
            (void)applyToTarget( settingIndex, state._committedValue, false ); // 확인 전 값으로 되돌린다. 대상이 없는 설정은 건너뛴다
        }
        SW_LOG_INFO( "Display change was not confirmed - reverted" );
        (void)saveIfPathSet(); // 경로가 없으면 저장하지 않는 실행이며, 쓰기 실패는 saveUserFile 이 경고로 남긴다
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
    bool UserSettingsManager::findBindingConflict( const hashed_string& settingID, string_view slotText, UserSettingBindingConflict& outConflict ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        if ( settingIndex == invalid_index::kUint32 )
            return false;
        const vector<UserSettingDef>& listSetting = _schema.getSettings();
        const UserSettingDef&         def         = listSetting[settingIndex];
        if ( def._type != UserSettingType::KeyBinding )
            return false;

        InputSlot  slot;
        InputMap*  pInputMap = _engineHandlers.findBindingInputMap( def._action );
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
                outConflict._settingID = other._id;
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
        outConflict._settingID = {};
        outConflict._action    = conflictingName;
        return true;
    }

    UserSettingSetResult UserSettingsManager::setPendingBinding( const hashed_string& settingID, string_view slotText, UserSettingBindingPolicy policy )
    {
        const uint32 settingIndex = findIndex( settingID );
        if ( settingIndex == invalid_index::kUint32 )
            return UserSettingSetResult::Unknown;
        if ( _schema.getSettings()[settingIndex]._type != UserSettingType::KeyBinding )
            return UserSettingSetResult::Rejected;

        UserSettingBindingConflict conflict;
        if ( findBindingConflict( settingID, slotText, conflict ) == false )
            return setPendingValue( settingID, slotText );
        if ( policy == UserSettingBindingPolicy::Reject || conflict._settingID.empty() )
            return UserSettingSetResult::Conflict;

        // 맞바꾸기: 겹친 설정이 이 설정의 지금 키를 받는다. 기본 키(빈 값)면 입력 맵에서 실제 키를 읽어 적는다.
        InputSlot currentSlot;
        string    currentText;
        if ( findEffectiveSlot( settingIndex, currentSlot ) )
            currentText = InputSlotUtil::toText( currentSlot );
        string normalized;
        if ( UserSettingsSchema::normalizeValue( _schema.getSettings()[settingIndex], slotText, normalized ) == UserSettingValueResult::Rejected )
            return UserSettingSetResult::Rejected;
        (void)setPendingAt( findIndex( conflict._settingID ), currentText, UserSettingValueResult::Accepted );
        return setPendingAt( settingIndex, normalized, UserSettingValueResult::Accepted );
    }

    string UserSettingsManager::getBindingGlyph( const hashed_string& settingID, InputGlyphStyle style ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        InputSlot    slot;
        if ( settingIndex == invalid_index::kUint32 || findEffectiveSlot( settingIndex, slot ) == false )
            return "[ ? ]";
        return InputMap::getGlyphForSlot( slot, style );
    }

    // ------------------------------------------------------------------------------
    // 7) 통보와 모듈
    // ------------------------------------------------------------------------------
    Subscription UserSettingsManager::registerEventListener( const UserSettingEventListener& listener )
    {
        return _onEvent.add( listener );
    }

    uint32 UserSettingsManager::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        return _onEvent.removeCodeWithin( pBegin, pEnd ) + _registry.removeCodeWithin( pBegin, pEnd );
    }

    // ------------------------------------------------------------------------------
    // 내부
    // ------------------------------------------------------------------------------
    uint32 UserSettingsManager::findIndex( const hashed_string& settingID ) const
    {
        const uint32 settingIndex = _schema.findSettingIndex( settingID );
        return settingIndex < static_cast<uint32>( _listState.size() ) ? settingIndex : invalid_index::kUint32;
    }

    uint32 UserSettingsManager::resolveKeyIndex( const hashed_string& settingID, UserSettingType expectedType ) const
    {
        const uint32 settingIndex = findIndex( settingID );
        if ( settingIndex == invalid_index::kUint32 )
        {
            SW_LOG_ERROR_ID( "UserSettings.KeyUnresolved", "User setting '%#' is not in the schema", settingID.c_str() );
            return invalid_index::kUint32;
        }
        const UserSettingType actualType = _schema.getSettings()[settingIndex]._type;
        if ( actualType != expectedType )
        {
            SW_LOG_ERROR_ID( "UserSettings.KeyUnresolved", "User setting '%#' is '%#', not '%#'", settingID.c_str(), UserSettingsSchema::getTypeTraits( actualType )._pName,
                             UserSettingsSchema::getTypeTraits( expectedType )._pName );
            return invalid_index::kUint32;
        }
        return settingIndex;
    }

    const string& UserSettingsManager::getKeyValue( [[maybe_unused]] const hashed_string& settingID, uint32 settingIndex ) const
    {
        // 핸들은 이 관리자의 스키마에서 풀렸다. 스키마를 비우고(`shutdown`) 다시 읽었는데 옛 핸들을 쓰면 다른 설정을 읽게 된다.
        SW_ASSERT( settingIndex < static_cast<uint32>( _listState.size() ) && _schema.getSettings()[settingIndex]._id == settingID );
        return getEffectiveValue( settingIndex );
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
            const uint32 otherIndex = findIndex( condition._settingID );
            if ( otherIndex == invalid_index::kUint32 )
                return false;
            const bool bEqual     = StringUtil::equals( getEffectiveValue( otherIndex ), condition._value );
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
                // 기동이나 다시 만들 때의 적용은 명령줄로 준 변수를 덮지 않는다. 실행 한 번의 값(명령줄)이 저장된 값(사용자 설정)을 이긴다(화면 설정과 같은 순서).
                // 메뉴에서 바꾼 값(bStartup 이 false)은 덮는다. 플레이어가 지금 고른 값이기 때문이다.
                const CommandLineManager* pCommandLineManager   = getTargets()._pCommandLineManager;
                const bool                bKeepCommandLineValue = bStartup && pCommandLineManager != nullptr && pCommandLineManager->isGlobalVariableProvided( def._targetName.view() );
                if ( bKeepCommandLineValue )
                    return true;
                if ( def._type != UserSettingType::Enum )
                    return pVariable->setValueFromString( value );

                // 열거형은 선택지의 `targetValue`, 없으면 선택지 순번을 넘긴다. 문자열 변수면 선택지 이름 그대로다.
                const uint32 optionIndex = findOptionIndex( def, value );
                if ( pVariable->_type == GlobalVariableType::String || optionIndex == invalid_index::kUint32 )
                    return pVariable->setValueFromString( value );
                const UserSettingOption& option = def._listOption[optionIndex];
                if ( option._targetValue.empty() == false )
                    return pVariable->setValueFromString( option._targetValue );
                return pVariable->setValueAsInt( static_cast<int32>( optionIndex ) );
            }
            case UserSettingTargetKind::Handler:
            {
                UserSettingApplyParams context;
                context._pDef     = &def;
                context._value    = value;
                context._param    = def._targetParam;
                context._bStartup = bStartup;
                context._bDefault = value == _listState[settingIndex]._defaultValue;
                return _registry.invokeHandler( def._targetName, context );
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
            (void)applyToTarget( settingIndex, normalized, false ); // 즉시 미리 본다. 대상이 없는 설정은 건너뛰고, 확정(commit)이 결과를 다시 확인한다
        broadcast( def._id, UserSettingEventKind::PendingChanged );
        return normalizeResult == UserSettingValueResult::Clamped ? UserSettingSetResult::Clamped : UserSettingSetResult::Accepted;
    }

    void UserSettingsManager::refreshScalabilityGroup( const ScalabilityGroupDef& group, bool bCommitted )
    {
        const uint32 groupIndex = findIndex( group._settingID );
        if ( groupIndex == invalid_index::kUint32 )
            return;

        string matchedPreset = group._customValue;
        for ( const ScalabilityPresetDef& preset : group._listPreset )
        {
            bool bMatches = true;
            for ( const ScalabilityPresetValue& value : preset._listValue )
            {
                const uint32 memberIndex = findIndex( value._settingID );
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
                const uint32 memberIndex = findIndex( value._settingID );
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
        // "custom" 은 프리셋이 없다. 묶인 설정은 그대로 둔다.
    }

    bool UserSettingsManager::findEffectiveSlot( uint32 settingIndex, InputSlot& outSlot ) const
    {
        const UserSettingDef& def   = _schema.getSettings()[settingIndex];
        const string&         value = getEffectiveValue( settingIndex );
        if ( value.empty() == false )
            return InputSlotUtil::tryParse( value, outSlot );
        const InputMap* pInputMap = _engineHandlers.findBindingInputMap( def._action );
        return pInputMap != nullptr && pInputMap->findDefaultRebindSlot( def._action, def._bindIndex, outSlot );
    }

    hashed_string UserSettingsManager::findBindingContext( uint32 settingIndex ) const
    {
        const UserSettingDef& def = _schema.getSettings()[settingIndex];
        if ( def._context.empty() == false )
            return def._context;
        const InputMap* pInputMap = _engineHandlers.findBindingInputMap( def._action );
        if ( pInputMap == nullptr )
            return {};
        const hashed_string layer = pInputMap->findBindingLayer( def._action, def._bindIndex );
        return layer.empty() ? pInputMap->getDefaultLayerName() : layer;
    }

    void UserSettingsManager::broadcast( const hashed_string& settingID, UserSettingEventKind kind )
    {
        UserSettingEvent event;
        event._settingID = settingID;
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

namespace sw
{
    SubsystemInitResult UserSettingsManager::initializeSubsystem()
    {
        UserSettingsTargets targets;
        targets._pGlobalVariableManager = &engine::getSubsystem<GlobalVariableManager>();
        targets._pCommandLineManager    = &engine::getSubsystem<CommandLineManager>();
        targets._pInputMap              = &engine::getSubsystem<InputManager>().getInputMap();
        targets._pAudioService          = &engine::getSubsystem<IAudioService>();
        targets._pLocalizationManager   = &engine::getSubsystem<LocalizationManager>();
        initialize( targets );

        const PackEngineSettings& packSettings = engine::getSubsystem<PackEngineSettings>();
        if ( loadSchema( ResourceID{ engine::getSubsystem<EngineDefaultAssets>()._userSettingsSchema.get() } ) && packSettings._userSettingsSchema.isSet() )
        {
            if ( loadSchema( ResourceID{ packSettings._userSettingsSchema.get() } ) == false )
                SW_LOG_ERROR( "Game user settings schema '%#' is not loaded", packSettings._userSettingsSchema.c_str() );
        }
        const EngineDefaultAssets& defaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        for ( DataPath EngineDefaultAssets::* pSchemaField : _listStartupSchemaField )
        {
            const DataPath& schemaPath = defaultAssets.*pSchemaField;
            if ( loadSchema( ResourceID{ schemaPath.get() } ) == false )
                SW_LOG_ERROR( "User settings schema '%#' is not loaded", schemaPath.c_str() );
        }
        for ( const auto& [settingID, value] : packSettings._mapUserSettingDefault )
        {
            (void)setGameDefault( hashed_string( settingID ), value );
        }

        // 사용자 폴더의 파일이다(세이브 게임과 별개). 자동화는 `-gv_userSettingsFile` 로 사용자 폴더를 건드리지 않는다.
        const string gameName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
        setUserFilePath( gv_userSettingsFile.empty() ? makeDefaultUserFilePath( gameName ) : gv_userSettingsFile );
        // 사양으로 품질 프리셋을 고르는 것은 사람이 처음 켠 실행만이다. 무인 실행(-unattended · -scenario)은 기계마다 다른 프리셋을 고르면 같은 시나리오의
        // 그림 · 시야 거리가 기계에 따라 달라진다(약한 GPU 의 low 가 지형 원경을 잘라 칠하기 비교가 0 이 됐다). 스키마 기본값 그대로 둔다 — 시나리오가
        // 품질을 보려면 <Launch> 로 정한다.
        const CommandLineManager& commandLine   = *targets._pCommandLineManager;
        const bool                bAutomatedRun = commandLine.isArgumentProvided( CommandLineArgument::UNATTENDED ) || commandLine.isArgumentProvided( CommandLineArgument::SCENARIO );
        if ( loadUserFile( getUserFilePath() ) == false && bAutomatedRun == false )
            (void)applyAutoDetectedPreset( HardwareProbe::probe() ); // 맞는 프리셋이 없으면 기본값 그대로다
        reapplyAll();
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> UserSettingsManager::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { IAudioService::kSubsystemName, InputManager::kSubsystemName, AssetManager::kSubsystemName,
                                                          PackEngineSettingsSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
