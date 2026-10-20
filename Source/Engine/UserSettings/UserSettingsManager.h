/**
 * @file UserSettingsManager.h
 * @brief 사용자 설정(플레이어 옵션 메뉴)의 값, 보류, 적용, 되돌리기, 저장과 메뉴 UI 가 부르는 바인딩 API 를 정의합니다.
 *
 * 흐름: 메뉴가 `setPendingValue` 로 보류 값을 넣으면 `applyPending` 이 대상(전역 변수, 핸들러)에 넣고 사용자 파일에 저장합니다.
 * 화면 방식처럼 `confirmSeconds` 가 있는 설정은 `confirmChanges` 를 그 시간 안에 부르지 않으면 `update` 가 되돌립니다.
 * 설정은 세이브 게임과 별개인 사용자 파일(사용자 폴더의 `usersettings.json`)에 저장합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/UserSettings/UserSettingsEngineHandlers.h"
#include "Engine/UserSettings/UserSettingsHandlerRegistry.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    enum class InputGlyphStyle : uint8;

    struct HardwareProbeResult;
    struct InputSlot;

    /** @brief 보류 값을 넣은 결과입니다. */
    enum class UserSettingSetResult : uint8
    {
        Accepted,  ///< 그대로 받았습니다.
        Clamped,   ///< 범위나 눈금으로 보정해 받았습니다.
        Unchanged, ///< 이미 그 값입니다.
        Rejected,  ///< 형식이 틀리거나 선택지에 없습니다.
        Unknown,   ///< 그런 설정이 없습니다.
        Disabled,  ///< 의존 조건이나 플랫폼 때문에 지금 바꿀 수 없습니다.
        Conflict,  ///< 키 바인딩이 다른 액션과 겹침(맞바꾸기를 고르지 않았거나 맞바꿀 수 없음)
    };

    /** @brief 키 바인딩이 겹칠 때의 처리입니다. */
    enum class UserSettingBindingPolicy : uint8
    {
        Reject, ///< 겹치면 받지 않는다(메뉴가 "이미 X 에 쓰입니다" 를 묻는다)
        Swap,   ///< 겹친 설정에 이 설정의 지금 키를 줍니다.
    };

    /** @brief 변경 통보의 종류입니다. */
    enum class UserSettingEventKind : uint8
    {
        PendingChanged,  ///< 보류 값이 바뀜(`_settingID`)
        Applied,         ///< 대상에 들어감(`_settingID`)
        Reverted,        ///< 보류 값을 버림(`_settingID`)
        ConfirmStarted,  ///< 확인 대기 시작(카운트다운)
        Confirmed,       ///< 확인했습니다.
        ConfirmTimedOut, ///< 확인하지 않아 되돌렸습니다.
        Loaded,          ///< 사용자 파일을 읽었거나 기본값으로 되돌리는 등 전체가 바뀌었습니다.
    };

    /** @brief 변경 통보 하나입니다. 전체 이벤트(확인, 로드)는 `_settingID` 가 비어 있습니다. */
    struct UserSettingEvent
    {
        hashed_string        _settingID{};
        UserSettingEventKind _kind{ UserSettingEventKind::PendingChanged };
    };
} // namespace sw

namespace sw
{
    SW_DECLARE_DELEGATE( void, UserSettingEventListener, const UserSettingEvent& );
    SW_DECLARE_MULTI_CAST_DELEGATE( void, UserSettingEventDelegate, const UserSettingEvent& );

    /** @brief `applyPending` 의 결과입니다. */
    struct UserSettingsApplyResult
    {
        uint32 _appliedCount{ 0 };         ///< 대상에 넣은 개수입니다.
        uint32 _failedCount{ 0 };          ///< 핸들러가 거절한 개수입니다.
        bool   _bNeedsRestart{ false };    ///< 다음 실행에 반영되는 값이 있는지 나타냅니다.
        bool   _bAwaitingConfirm{ false }; ///< 확인 대기(카운트다운)가 시작됐는지 나타냅니다.
        bool   _bSaved{ false };           ///< 사용자 파일에 썼는지 나타냅니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 키 바인딩 겹침 하나입니다. `_settingID` 가 비면 스키마 밖의 액션(`_action`)과 겹친 것입니다. */
    struct UserSettingBindingConflict
    {
        hashed_string _settingID{};
        hashed_string _action{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class UserSettingsManager
     * @brief 엔진 서비스(`engine::getUserSettingsManager`, 게임에서도 보입니다)입니다. 스키마, 값, 사용자 파일, 핸들러 등록부를 가집니다.
     * @details **게임 스레드 전용**이다. 대상 적용은 게임 스레드에서 일어나고, 창과 스왑체인 변경은 화면 요청으로 미뤄 호스트가 프레임 맨 앞에서
     *          처리한다(`consumeDisplayRequest`). 모듈이 등록한 통보와 핸들러는 모듈을 언로드하기 전에 제거한다(IModuleUnloadListener).
     */
    class SW_API UserSettingsManager : public IModuleUnloadListener, public ISubsystem
    {
    public:
        UserSettingsManager();
        ~UserSettingsManager() override;
        UserSettingsManager( const UserSettingsManager& )            = delete;
        UserSettingsManager& operator=( const UserSettingsManager& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 수명과 스키마
        // ------------------------------------------------------------------------------
        /** @brief 대상을 정하고 엔진 핸들러를 올립니다. 스키마는 비어 있습니다. */
        void initialize( const UserSettingsTargets& targets );
        /** @brief 값, 스키마, 통보, 핸들러를 모두 비웁니다. */
        void shutdown();
        /**
         * @brief 키 바인딩 설정의 대상 맵을 더합니다(런타임 UI 의 행동 맵 등). 그 맵에 액션이 있는 키 바인딩 설정의 확정 값을 바로 넣습니다.
         * @details 키 바인딩 설정의 대상은 그 액션을 가진 맵이다 — 게임 맵(`UserSettingsTargets::_pInputMap`)을 먼저 보고, 없으면 더한 맵을 차례로 본다.
         *          겹침 검사의 문맥(레이어)도 그 맵에서 읽는다. 언리얼 Enhanced Input 의 사용자 설정이 등록된 모든 매핑 컨텍스트의 키를 바꾸는 것과 같다.
         *          맵의 주인이 내려가기 전에 `removeKeyBindingInputMap` 으로 뗀다.
         */
        void addKeyBindingInputMap( InputMap& inputMap );
        void removeKeyBindingInputMap( const InputMap& inputMap );

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "UserSettings";
        /**
         * @brief 기동 순서의 자리 — 대상(전역 변수 · 명령줄 · 입력 맵 · 오디오 · 로컬라이제이션)을 묶고, 엔진 · 게임 스키마와 기동 전에 더한 스키마(`addStartupSchema`)를
         *        읽은 뒤 사용자 파일(없으면 하드웨어로 고른 프리셋)을 적용합니다. 화면 값은 창을 만드는 RHI 가 읽는다.
         * @details 스키마 · 사용자 파일이 틀려도 기동은 멈추지 않는다(오류로 알리고 옵션 메뉴가 비거나 기본값이다) — 화면을 못 띄우는 설정 파일 하나로
         *          게임이 안 뜨면 플레이어가 고칠 길이 없다.
         */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override { shutdown(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        /**
         * @brief `initializeSubsystem` 이 사용자 파일보다 먼저 읽을 스키마를 더합니다(개발 도구의 스키마 — 저장된 값이 버려지지 않게). 기동 전에 부릅니다.
         * @details 경로는 엔진 기본 에셋 표의 칸 @p pSchemaField 로 준다 — 표는 기동 단계에서 읽히므로 그때 그 칸의 값을 쓴다(코드에 경로를 적지 않는다).
         */
        void addStartupSchema( DataPath EngineDefaultAssets::* pSchemaField ) { _listStartupSchemaField.push_back( pSchemaField ); }

        /** @brief 스키마 XML 을 덧붙이고 검사합니다. 실패하면 덧붙인 것을 버리고 false 입니다. 새 설정은 기본값으로 시작합니다. */
        [[nodiscard]] bool loadSchema( string_view resourcePath );
        /** @brief 위와 같되 XML 글에서 읽습니다. */
        [[nodiscard]] bool loadSchemaFromXMLText( string_view xmlText, string_view sourceName );
        /**
         * @brief 팩 설정의 기본값 덮어쓰기(`PackEngineSettings::_mapUserSettingDefault`)를 등록합니다. 아직 바꾸지 않은 값(기본값)도 따라 바뀝니다.
         * @return 모르는 설정이거나 받을 수 없는 값이면 오류를 알리고 false 입니다.
         */
        [[nodiscard]] bool setGameDefault( const hashed_string& settingID, string_view value );

        // ------------------------------------------------------------------------------
        // 2) 사용자 파일: 세이브 게임과 별개이며, 버전 번호와 버전 올리기 단계가 있는 플레이어 데이터
        // ------------------------------------------------------------------------------
        /**
         * @brief 사용자 파일을 읽어 확정 값으로 씁니다. 파일이 없으면 false 입니다(첫 실행이며 오류가 아닙니다).
         *        파일이 깨졌으면(읽기 · JSON 실패) 오류를 남기고 `<경로>.corrupt` 로 치운 뒤 false 입니다 — 다음 저장이 새 파일을 만듭니다.
         * @details 이전 버전 파일은 스키마의 `<Upgrade>` 단계로 올린다. 모르는 키는 경고하고 버리고, 범위 밖 값은 맞춰 넣고(경고), 형식이 틀린 값은 기본값으로 둔다.
         */
        [[nodiscard]] bool loadUserFile( string_view filePath );
        /** @brief 위와 같되 JSON 글에서 읽습니다. */
        [[nodiscard]] bool loadUserJSON( string_view jsonText, string_view sourceName );
        /** @brief 기본값과 다른 확정 값만 사용자 파일에 씁니다. 확인 대기 중인 설정은 되돌릴 값(옛 값)을 씁니다. */
        [[nodiscard]] bool saveUserFile( string_view filePath ) const;
        /** @brief `saveUserFile` 이 쓰는 JSON 글입니다. */
        string makeUserJSON() const;
        /** @brief `applyPending` 이 저장할 경로입니다. 비면 저장하지 않습니다(테스트와 헤드리스). */
        void          setUserFilePath( string_view filePath ) { _userFilePath = string( filePath ); }
        const string& getUserFilePath() const { return _userFilePath; }
        /** @brief 사용자 파일을 읽었는지(첫 실행이 아닌지) 반환합니다. */
        bool hasLoadedUserFile() const { return _bUserFileLoaded; }
        /**
         * @brief 사용자 폴더의 설정 파일 경로입니다. Windows 는 `%LOCALAPPDATA%/SWEngine/<game>/usersettings.json` 이고,
         *        Linux 는 `$XDG_CONFIG_HOME`(없으면 `~/.config`)`/swengine/<game>/usersettings.json` 입니다. 사용자 폴더를 모르면 `Saved/<game>/` 를 씁니다(`UserDataPath::getConfigDirectory`).
         */
        static string makeDefaultUserFilePath( string_view gameName );

        // ------------------------------------------------------------------------------
        // 3) 기동: 품질 자동 선택과 전체 적용
        // ------------------------------------------------------------------------------
        /** @brief 사양에 맞는 품질 프리셋 이름입니다(첫 품질 번들에서 규칙 순서대로 첫 일치를 고르고, 없으면 `autoDetectFallback`). 번들이 없으면 빈 이름입니다. */
        hashed_string detectScalabilityPreset( const HardwareProbeResult& probe ) const;
        /** @brief 자동 선택한 프리셋을 확정 값으로 씁니다(적용과 저장은 하지 않으며, 이어지는 `reapplyAll` 이 넣습니다). */
        [[nodiscard]] bool applyAutoDetectedPreset( const HardwareProbeResult& probe );
        /**
         * @brief 모든 확정 값을 대상에 다시 넣습니다(기동 적용). 게임이 언어 팩과 입력 맵을 만든 뒤에도 부릅니다. 그 대상은 그때 생기기 때문입니다.
         * @details 첫 호출(엔진 기동)은 `NeedsRestart` 설정도 넣는다(이것이 "다음 실행" 의 적용이다). 그 뒤의 호출은 그 설정에 기동 때 값을 다시 넣는다.
         */
        void reapplyAll();

        // ------------------------------------------------------------------------------
        // 4) 메뉴 UI 바인딩: 나열, 값, 상태
        // ------------------------------------------------------------------------------
        const vector<UserSettingCategoryDef>& getCategories() const { return _schema.getCategories(); }
        /** @brief 카테고리의 설정을 표시 순서로 모읍니다. 이 플랫폼에서 쓸 수 없는 설정은 빠집니다. */
        void                      collectSettings( const hashed_string& categoryID, vector<const UserSettingDef*>& outListSetting ) const;
        const UserSettingDef*     findSetting( const hashed_string& settingID ) const { return _schema.findSetting( settingID ); }
        const UserSettingsSchema& getSchema() const { return _schema; }
        /** @brief 메뉴가 보일 값입니다. 보류 값이 있으면 그것이고, 없으면 확정 값입니다. 없는 설정은 빈 문자열입니다. */
        string_view getValue( const hashed_string& settingID ) const;
        /** @brief 확정 값(적용 · 저장된 값)입니다. */
        string_view getAppliedValue( const hashed_string& settingID ) const;
        /** @brief 기본값(게임 프리셋 덮어쓰기 포함)입니다. */
        string_view getDefaultValue( const hashed_string& settingID ) const;
        bool        getBoolValue( const hashed_string& settingID ) const;
        int32       getIntValue( const hashed_string& settingID ) const;
        float32     getFloatValue( const hashed_string& settingID ) const;
        /** @brief 열거형의 선택지입니다(공급자 설정은 지금 공급자가 주는 것). */
        void collectOptions( const hashed_string& settingID, vector<UserSettingOption>& outListOption ) const;

        /** @brief 보류 값을 넣습니다. `Immediate` 설정은 바로 대상에 미리 적용합니다. 품질 번들의 프리셋과 Custom 도 함께 맞춥니다. */
        UserSettingSetResult setPendingValue( const hashed_string& settingID, string_view value );
        UserSettingSetResult setPendingBoolValue( const hashed_string& settingID, bool bValue );
        UserSettingSetResult setPendingIntValue( const hashed_string& settingID, int32 value );
        UserSettingSetResult setPendingFloatValue( const hashed_string& settingID, float32 value );

        bool isPending( const hashed_string& settingID ) const;
        bool hasPendingChanges() const;
        /** @brief 이 플랫폼에서 쓰고 의존 조건(`enabledWhen`)을 모두 만족하면 true 입니다(메뉴가 회색으로 보일지). */
        bool isSettingEnabled( const hashed_string& settingID ) const;
        /** @brief 이 플랫폼에서 쓰는 설정이면 true 입니다. */
        bool isSettingAvailable( const hashed_string& settingID ) const;

        // ------------------------------------------------------------------------------
        // 5) 적용, 되돌리기, 기본값, 확인 카운트다운
        // ------------------------------------------------------------------------------
        /** @brief 보류 값을 모두 확정하고 대상에 넣고 저장합니다. 바뀐 값에 `confirmSeconds` 가 있으면 확인 대기를 시작합니다. */
        UserSettingsApplyResult applyPending();
        /** @brief 보류 값을 모두 버립니다. 미리 적용한 `Immediate` 값은 확정 값으로 되돌립니다. */
        void revertPending();
        /** @brief 카테고리의 설정을 기본값으로 보류합니다(`applyPending` 으로 확정). */
        void resetCategoryToDefaults( const hashed_string& categoryID );
        /** @brief 확인 대기 중인지 반환합니다. */
        bool isAwaitingConfirm() const { return _confirmSecondsLeft > 0.0f; }
        /** @brief 확인 대기의 남은 초입니다(메뉴의 "N 초 뒤 되돌립니다"). */
        float32 getConfirmSecondsLeft() const { return _confirmSecondsLeft; }
        /** @brief 확인 대기 중인 값을 받아들입니다. */
        void confirmChanges();
        /** @brief 확인 대기 카운트다운을 진행합니다. 0 이 되면 바뀐 값을 되돌려 적용하고 저장합니다. 호스트가 프레임마다 부릅니다. */
        void update( float32 deltaSeconds );
        /** @brief 다음 실행에 닿을 값(`NeedsRestart`)이 확정돼 있으면 true 입니다. */
        bool isRestartRequired() const;

        // ------------------------------------------------------------------------------
        // 6) 키 바인딩과 글리프
        // ------------------------------------------------------------------------------
        /** @brief @p slotText 를 이 설정에 줄 때 겹치는 것을 찾습니다. 같은 범위(`context` 와 입력 맵 레이어)의 다른 키 바인딩 설정을 먼저 보고, 그다음 스키마 밖 액션을 봅니다. */
        bool findBindingConflict( const hashed_string& settingID, string_view slotText, UserSettingBindingConflict& outConflict ) const;
        /** @brief 키를 보류합니다. 겹치면 @p policy 대로 거절하거나 맞바꿉니다(겹친 설정에 이 설정의 지금 키). */
        UserSettingSetResult setPendingBinding( const hashed_string& settingID, string_view slotText, UserSettingBindingPolicy policy );
        /** @brief 메뉴가 보일 키 표기(보류 값 기준, 빈 값이면 입력 맵의 기본 키)입니다. 모르면 `[ ? ]` 입니다. */
        string getBindingGlyph( const hashed_string& settingID, InputGlyphStyle style ) const;

        // ------------------------------------------------------------------------------
        // 7) 통보, 레지스트리, 화면 요청
        // ------------------------------------------------------------------------------
        DelegateHandle registerEventListener( const UserSettingEventListener& listener );
        void           unregisterEventListener( const DelegateHandle& handle );
        /** @brief 변경 통보를 받는 리스너가 하나라도 있으면 true 입니다(화면이 닫힐 때 해제했는지 테스트가 확인합니다). */
        bool                         hasEventListener() const { return _onEvent.isBound(); }
        UserSettingsHandlerRegistry& getRegistry() { return _registry; }
        const UserSettingsTargets&   getTargets() const { return _engineHandlers.getTargets(); }
        /** @brief 화면 설정의 지금 요청입니다(기동 때 창과 스왑체인을 만드는 쪽이 읽습니다). */
        const DisplaySettingsRequest& getDisplayRequest() const { return _engineHandlers.getDisplayRequest(); }
        /** @brief 실행 중에 바뀐 화면 요청을 꺼냅니다(호스트가 프레임 맨 앞에서). */
        bool consumeDisplayRequest( DisplaySettingsRequest& outRequest ) { return _engineHandlers.consumeDisplayRequest( outRequest ); }

        const utf8* getModuleUnloadListenerName() const override { return "user settings"; }
        uint32      onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        /** @brief 설정 하나의 값 상태입니다. `UserSettingsSchema::getSettings` 와 같은 순번입니다. */
        struct SettingState
        {
            string _defaultValue{};   ///< 기본값입니다(게임 프리셋 덮어쓰기 포함).
            string _committedValue{}; ///< 확정 값입니다.
            string _pendingValue{};   ///< 보류 값(`_bPending` 일 때)
            string _rollbackValue{};  ///< 확인 대기 중 되돌릴 값(`_bRollback` 일 때)
            string _startupValue{};   ///< 기동 적용 때 넣은 값(`NeedsRestart` 판정)
            bool   _bPending{ false };
            bool   _bRollback{ false };
        };

        bool appendSchema( bool bLoaded );
        /**
         * @brief 설정의 지금 선택지를 모읍니다. 공급자가 있으면 공급자 것, 비었으면 고정 `<Option>` 이 대체합니다.
         * @details 공급자와 고정 선택지를 함께 적은 설정(해상도)은 기본값과 지금 값이 목록에 없으면 뒤에 붙인다. 모니터를 바꿔도 현재 선택이 목록에서 사라지지 않게 하기 위해서다.
         */
        void                 collectOptionsOf( const UserSettingDef& def, vector<UserSettingOption>& outListOption ) const;
        uint32               findIndex( const hashed_string& settingID ) const;
        const string&        getEffectiveValue( uint32 settingIndex ) const;
        bool                 isEnabledAt( uint32 settingIndex ) const;
        bool                 isAvailableAt( uint32 settingIndex ) const;
        [[nodiscard]] bool   applyToTarget( uint32 settingIndex, const string& value, bool bStartup );
        UserSettingSetResult setPendingAt( uint32 settingIndex, const string& normalized, UserSettingValueResult normalizeResult );
        void                 refreshScalabilityGroup( const ScalabilityGroupDef& group, bool bCommitted );
        void                 applyScalabilityPreset( const ScalabilityGroupDef& group, const hashed_string& presetName, bool bCommitted );
        bool                 findEffectiveSlot( uint32 settingIndex, InputSlot& outSlot ) const;
        hashed_string        findBindingContext( uint32 settingIndex ) const;
        void                 broadcast( const hashed_string& settingID, UserSettingEventKind kind );
        [[nodiscard]] bool   saveIfPathSet();

    private:
        UserSettingsSchema                      _schema;
        UserSettingsHandlerRegistry             _registry;
        UserSettingsEngineHandlers              _engineHandlers;
        vector<SettingState>                    _listState;
        UserSettingEventDelegate                _onEvent;
        string                                  _userFilePath;
        float32                                 _confirmSecondsLeft;
        bool                                    _bUserFileLoaded;
        bool                                    _bStartupApplied;        ///< 첫 `reapplyAll`(기동 적용)이 지났는지 나타냅니다. 그 뒤로 `NeedsRestart` 값은 바뀌지 않습니다.
        vector<DataPath EngineDefaultAssets::*> _listStartupSchemaField; ///< `addStartupSchema` 가 더한 스키마 칸(기동이 사용자 파일보다 먼저 읽는다)
    };
} // namespace sw
