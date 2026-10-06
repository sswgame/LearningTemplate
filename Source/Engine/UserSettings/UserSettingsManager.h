/**
 * @file UserSettingsManager.h
 * @brief 사용자 설정(플레이어 옵션 메뉴)의 값 · 보류 · 적용 · 되돌리기 · 저장과, 메뉴 UI 가 부르는 바인딩 API 입니다.
 *
 * 흐름: 메뉴가 `setPendingValue` 로 보류 값을 넣는다 → `applyPending` 이 대상(전역 변수 · 적용기)에 넣고 사용자 파일에 저장한다 →
 * 화면 방식처럼 `confirmSeconds` 가 있는 설정은 `confirmChanges` 를 그 시간 안에 부르지 않으면 `update` 가 되돌린다.
 * 세이브 게임과 별개인 사용자 파일(사용자 폴더의 `usersettings.json`)에 저장합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/hashed_string.h"

#include "Engine/UserSettings/UserSettingsEngineAppliers.h"
#include "Engine/UserSettings/UserSettingsRegistry.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    enum class InputGlyphStyle : uint8;

    struct HardwareProbeResult;
    struct InputSlot;

    /** @brief 보류 값을 넣은 결과입니다. */
    enum class UserSettingSetResult : uint8
    {
        Accepted,  ///< 그대로 받음
        Clamped,   ///< 범위 · 눈금으로 고쳐 받음
        Unchanged, ///< 이미 그 값
        Rejected,  ///< 형식이 틀리거나 선택지에 없음
        Unknown,   ///< 그런 설정이 없음
        Disabled,  ///< 의존 조건 · 플랫폼 때문에 지금 바꿀 수 없음
        Conflict,  ///< 키 바인딩이 다른 액션과 겹침(맞바꾸기를 고르지 않았거나 맞바꿀 수 없음)
    };

    /** @brief 키 바인딩이 겹칠 때의 처리입니다. */
    enum class UserSettingBindingPolicy : uint8
    {
        Reject, ///< 겹치면 받지 않는다(메뉴가 "이미 X 에 쓰입니다" 를 묻는다)
        Swap,   ///< 겹친 설정에 이 설정의 지금 키를 준다
    };

    /** @brief 변경 통보의 종류입니다. */
    enum class UserSettingEventKind : uint8
    {
        PendingChanged,  ///< 보류 값이 바뀜(`_settingId`)
        Applied,         ///< 대상에 들어감(`_settingId`)
        Reverted,        ///< 보류 값을 버림(`_settingId`)
        ConfirmStarted,  ///< 확인 대기 시작(카운트다운)
        Confirmed,       ///< 확인됨
        ConfirmTimedOut, ///< 확인하지 않아 되돌림
        Loaded,          ///< 사용자 파일을 읽음 · 기본값으로 되돌림 등 전체가 바뀜
    };

    /** @brief 변경 통보 하나입니다. 전체 사건(확인 · 로드)은 `_settingId` 가 비어 있습니다. */
    struct UserSettingEvent
    {
        hashed_string        _settingId{};
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
        uint32 _appliedCount{ 0 };         ///< 대상에 넣은 수
        uint32 _failedCount{ 0 };          ///< 적용기가 거절한 수
        bool   _bNeedsRestart{ false };    ///< 다음 실행에 닿는 값이 있음
        bool   _bAwaitingConfirm{ false }; ///< 확인 대기(카운트다운)가 시작됨
        bool   _bSaved{ false };           ///< 사용자 파일에 썼음
    };
} // namespace sw

namespace sw
{
    /** @brief 키 바인딩 겹침 하나입니다. `_settingId` 가 비면 스키마 밖의 액션(`_action`)과 겹친 것입니다. */
    struct UserSettingBindingConflict
    {
        hashed_string _settingId{};
        hashed_string _action{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class UserSettingsManager
     * @brief 엔진 서비스(`engine::getUserSettingsManager`, 게임에도 보임) — 스키마 · 값 · 사용자 파일 · 적용기 등록부를 가집니다.
     * @details **게임 스레드 전용**입니다. 대상 적용은 게임 스레드에서 일어나고, 창 · 스왑체인 변경은 화면 요청으로 미뤄 호스트가 프레임 맨 앞에서
     *          합니다(`consumeDisplayRequest`). 모듈이 건 통보 · 적용기는 모듈을 내리기 전에 뗍니다(IModuleUnloadListener).
     */
    class SW_API UserSettingsManager : public IModuleUnloadListener
    {
    public:
        UserSettingsManager();
        ~UserSettingsManager() override;
        UserSettingsManager( const UserSettingsManager& )            = delete;
        UserSettingsManager& operator=( const UserSettingsManager& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 수명 · 스키마
        // ------------------------------------------------------------------------------
        /** @brief 대상을 정하고 엔진 적용기를 올립니다. 스키마는 비어 있습니다. */
        void initialize( const UserSettingsTargets& targets );
        /** @brief 값 · 스키마 · 통보 · 적용기를 모두 비웁니다. */
        void shutdown();

        /** @brief 스키마 XML 을 덧붙이고 검사합니다. 실패하면 덧붙인 것을 버리고 false 입니다. 새 설정은 기본값으로 시작합니다. */
        [[nodiscard]] bool loadSchema( string_view resourcePath );
        /** @brief 위와 같되 XML 글에서 읽습니다. */
        [[nodiscard]] bool loadSchemaFromXmlText( string_view xmlText, string_view sourceName );
        /**
         * @brief 게임 프리셋의 기본값 덮어쓰기(`GameConfig::_mapUserSettingDefault`)를 겁니다. 아직 바꾸지 않은 값(기본값)도 따라 바뀝니다.
         * @return 모르는 설정 · 받을 수 없는 값이면 오류를 알리고 false 입니다.
         */
        [[nodiscard]] bool setGameDefault( const hashed_string& settingId, string_view value );

        // ------------------------------------------------------------------------------
        // 2) 사용자 파일 — 세이브 게임과 별개, 판 번호 · 버전 올리기 단계가 있는 플레이어 데이터
        // ------------------------------------------------------------------------------
        /**
         * @brief 사용자 파일을 읽어 확정 값으로 씁니다. 파일이 없으면 false 입니다(첫 실행 — 오류가 아님).
         * @details 옛 판 파일은 스키마의 `<Upgrade>` 단계로 올립니다. 모르는 키는 경고와 함께 버리고, 범위 밖 값은 맞춰 넣고(경고), 형식이 틀린 값은 기본값으로 둡니다.
         */
        [[nodiscard]] bool loadUserFile( string_view filePath );
        /** @brief 위와 같되 JSON 글에서 읽습니다. */
        [[nodiscard]] bool loadUserJson( string_view jsonText, string_view sourceName );
        /** @brief 기본값과 다른 확정 값만 사용자 파일에 씁니다. 확인 대기 중인 설정은 되돌릴 값(옛 값)을 씁니다. */
        [[nodiscard]] bool saveUserFile( string_view filePath ) const;
        /** @brief `saveUserFile` 이 쓰는 JSON 글입니다. */
        string makeUserJson() const;
        /** @brief `applyPending` 이 저장할 경로입니다. 비면 저장하지 않습니다(시험 · 헤드리스). */
        void          setUserFilePath( string_view filePath ) { _userFilePath = string( filePath ); }
        const string& getUserFilePath() const { return _userFilePath; }
        /** @brief 사용자 파일을 읽었는지(첫 실행이 아닌지) 반환합니다. */
        bool hasLoadedUserFile() const { return _bUserFileLoaded; }
        /**
         * @brief 사용자 폴더의 설정 파일 경로입니다 — Windows `%LOCALAPPDATA%/SWEngine/<game>/usersettings.json`,
         *        Linux `$XDG_CONFIG_HOME`(없으면 `~/.config`)`/swengine/<game>/usersettings.json`. 사용자 폴더를 모르면 `Saved/<game>/` 입니다(`UserDataPath::getConfigDirectory`).
         */
        static string makeDefaultUserFilePath( string_view gameName );

        // ------------------------------------------------------------------------------
        // 3) 기동 — 품질 자동 선택 · 전체 적용
        // ------------------------------------------------------------------------------
        /** @brief 사양에 맞는 품질 프리셋 이름입니다(첫 품질 묶음, 규칙 순서대로 첫 일치 → `autoDetectFallback`). 묶음이 없으면 빈 이름입니다. */
        hashed_string detectScalabilityPreset( const HardwareProbeResult& probe ) const;
        /** @brief 자동 선택한 프리셋을 확정 값으로 씁니다(적용 · 저장 없이 — 이어지는 `reapplyAll` 이 넣습니다). */
        [[nodiscard]] bool applyAutoDetectedPreset( const HardwareProbeResult& probe );
        /**
         * @brief 모든 확정 값을 대상에 다시 넣습니다(기동 적용). 게임이 언어 팩 · 입력 맵을 세운 뒤에도 부릅니다 — 그 대상은 그때 생긴다.
         * @details 첫 부름(엔진 기동)은 `NeedsRestart` 설정도 넣습니다(이것이 "다음 실행" 의 적용입니다). 그 뒤의 부름은 그 설정에 기동 때 값을 다시 넣습니다.
         */
        void reapplyAll();

        // ------------------------------------------------------------------------------
        // 4) 메뉴 UI 바인딩 — 나열 · 값 · 상태
        // ------------------------------------------------------------------------------
        const vector<UserSettingCategoryDef>& getCategories() const { return _schema.getCategories(); }
        /** @brief 카테고리의 설정을 표시 순서로 모읍니다. 이 플랫폼에서 쓸 수 없는 설정은 빠집니다. */
        void                      collectSettings( const hashed_string& categoryId, vector<const UserSettingDef*>& outListSetting ) const;
        const UserSettingDef*     findSetting( const hashed_string& settingId ) const { return _schema.findSetting( settingId ); }
        const UserSettingsSchema& getSchema() const { return _schema; }
        /** @brief 메뉴가 보일 값 — 보류 값이 있으면 그것, 없으면 확정 값입니다. 없는 설정은 빈 글입니다. */
        string_view getValue( const hashed_string& settingId ) const;
        /** @brief 확정 값(적용 · 저장된 값)입니다. */
        string_view getAppliedValue( const hashed_string& settingId ) const;
        /** @brief 기본값(게임 프리셋 덮어쓰기 포함)입니다. */
        string_view getDefaultValue( const hashed_string& settingId ) const;
        bool        getBoolValue( const hashed_string& settingId ) const;
        int32       getIntValue( const hashed_string& settingId ) const;
        float32     getFloatValue( const hashed_string& settingId ) const;
        /** @brief 열거형의 선택지입니다(공급자 설정은 지금 공급자가 주는 것). */
        void collectOptions( const hashed_string& settingId, vector<UserSettingOption>& outListOption ) const;

        /** @brief 보류 값을 넣습니다. `Immediate` 설정은 바로 대상에 미리 적용합니다. 품질 묶음의 프리셋 · Custom 을 함께 맞춥니다. */
        UserSettingSetResult setPendingValue( const hashed_string& settingId, string_view value );
        UserSettingSetResult setPendingBoolValue( const hashed_string& settingId, bool bValue );
        UserSettingSetResult setPendingIntValue( const hashed_string& settingId, int32 value );
        UserSettingSetResult setPendingFloatValue( const hashed_string& settingId, float32 value );

        bool isPending( const hashed_string& settingId ) const;
        bool hasPendingChanges() const;
        /** @brief 이 플랫폼에서 쓰고 의존 조건(`enabledWhen`)을 모두 만족하면 true 입니다(메뉴가 회색으로 보일지). */
        bool isSettingEnabled( const hashed_string& settingId ) const;
        /** @brief 이 플랫폼에서 쓰는 설정이면 true 입니다. */
        bool isSettingAvailable( const hashed_string& settingId ) const;

        // ------------------------------------------------------------------------------
        // 5) 적용 · 되돌리기 · 기본값 · 확인 카운트다운
        // ------------------------------------------------------------------------------
        /** @brief 보류 값을 모두 확정하고 대상에 넣고 저장합니다. 바뀐 값에 `confirmSeconds` 가 있으면 확인 대기를 시작합니다. */
        UserSettingsApplyResult applyPending();
        /** @brief 보류 값을 모두 버립니다. 미리 적용한 `Immediate` 값은 확정 값으로 되돌립니다. */
        void revertPending();
        /** @brief 카테고리의 설정을 기본값으로 보류합니다(`applyPending` 으로 확정). */
        void resetCategoryToDefaults( const hashed_string& categoryId );
        /** @brief 확인 대기 중인지 반환합니다. */
        bool isAwaitingConfirm() const { return _confirmSecondsLeft > 0.0f; }
        /** @brief 확인 대기의 남은 초입니다(메뉴의 "N 초 뒤 되돌립니다"). */
        float32 getConfirmSecondsLeft() const { return _confirmSecondsLeft; }
        /** @brief 확인 대기 중인 값을 받아들입니다. */
        void confirmChanges();
        /** @brief 확인 대기 카운트다운을 진행합니다. 0 이 되면 바뀐 값을 되돌려 적용 · 저장합니다. 호스트가 프레임마다 부릅니다. */
        void update( float32 deltaSeconds );
        /** @brief 다음 실행에 닿을 값(`NeedsRestart`)이 확정돼 있으면 true 입니다. */
        bool isRestartRequired() const;

        // ------------------------------------------------------------------------------
        // 6) 키 바인딩 · 글리프
        // ------------------------------------------------------------------------------
        /** @brief @p slotText 를 이 설정에 줄 때 겹치는 것을 찾습니다. 같은 범위(`context` · 입력 맵 레이어)의 다른 키 바인딩 설정, 그다음 스키마 밖 액션. */
        bool findBindingConflict( const hashed_string& settingId, string_view slotText, UserSettingBindingConflict& outConflict ) const;
        /** @brief 키를 보류합니다. 겹치면 @p policy 대로 거절하거나 맞바꿉니다(겹친 설정에 이 설정의 지금 키). */
        UserSettingSetResult setPendingBinding( const hashed_string& settingId, string_view slotText, UserSettingBindingPolicy policy );
        /** @brief 메뉴가 보일 키 표기(보류 값 기준, 빈 값이면 입력 맵의 기본 키)입니다. 모르면 `[ ? ]` 입니다. */
        string getBindingGlyph( const hashed_string& settingId, InputGlyphStyle style ) const;

        // ------------------------------------------------------------------------------
        // 7) 통보 · 등록부 · 화면 요청
        // ------------------------------------------------------------------------------
        DelegateHandle              registerEventListener( const UserSettingEventListener& listener );
        void                        unregisterEventListener( const DelegateHandle& handle );
        UserSettingApplierRegistry& getRegistry() { return _registry; }
        const UserSettingsTargets&  getTargets() const { return _engineAppliers.getTargets(); }
        /** @brief 화면 설정의 지금 요청입니다(기동 때 창 · 스왑체인을 만드는 쪽이 읽습니다). */
        const DisplaySettingsRequest& getDisplayRequest() const { return _engineAppliers.getDisplayRequest(); }
        /** @brief 실행 중에 바뀐 화면 요청을 꺼냅니다(호스트가 프레임 맨 앞에서). */
        bool consumeDisplayRequest( DisplaySettingsRequest& outRequest ) { return _engineAppliers.consumeDisplayRequest( outRequest ); }

        const utf8* getModuleUnloadListenerName() const override { return "user settings"; }
        uint32      onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        /** @brief 설정 하나의 값 상태입니다. `UserSettingsSchema::getSettings` 와 같은 순번입니다. */
        struct SettingState
        {
            string _defaultValue{};   ///< 기본값(게임 프리셋 덮어쓰기 포함)
            string _committedValue{}; ///< 확정 값
            string _pendingValue{};   ///< 보류 값(`_bPending` 일 때)
            string _rollbackValue{};  ///< 확인 대기 중 되돌릴 값(`_bRollback` 일 때)
            string _startupValue{};   ///< 기동 적용 때 넣은 값(`NeedsRestart` 판정)
            bool   _bPending{ false };
            bool   _bRollback{ false };
        };

        bool                 appendSchema( bool bLoaded );
        uint32               findIndex( const hashed_string& settingId ) const;
        const string&        getEffectiveValue( uint32 settingIndex ) const;
        bool                 isEnabledAt( uint32 settingIndex ) const;
        bool                 isAvailableAt( uint32 settingIndex ) const;
        [[nodiscard]] bool   applyToTarget( uint32 settingIndex, const string& value, bool bStartup );
        UserSettingSetResult setPendingAt( uint32 settingIndex, const string& normalized, UserSettingValueResult normalizeResult );
        void                 refreshScalabilityGroup( const ScalabilityGroupDef& group, bool bCommitted );
        void                 applyScalabilityPreset( const ScalabilityGroupDef& group, const hashed_string& presetName, bool bCommitted );
        bool                 findEffectiveSlot( uint32 settingIndex, InputSlot& outSlot ) const;
        hashed_string        findBindingContext( uint32 settingIndex ) const;
        void                 broadcast( const hashed_string& settingId, UserSettingEventKind kind );
        [[nodiscard]] bool   saveIfPathSet();

    private:
        UserSettingsSchema         _schema;
        UserSettingApplierRegistry _registry;
        UserSettingsEngineAppliers _engineAppliers;
        vector<SettingState>       _listState;
        UserSettingEventDelegate   _onEvent;
        string                     _userFilePath;
        float32                    _confirmSecondsLeft;
        bool                       _bUserFileLoaded;
        bool                       _bStartupApplied; ///< 첫 `reapplyAll`(기동 적용)이 지났는지 — 그 뒤로 `NeedsRestart` 값은 바뀌지 않는다
    };
} // namespace sw
