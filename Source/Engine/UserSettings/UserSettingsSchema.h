/**
 * @file UserSettingsSchema.h
 * @brief 사용자 설정(옵션 메뉴)의 스키마와 그 XML 로더를 정의합니다. 스키마는 카테고리, 설정 정의, 그래픽 품질 프리셋, 사용자 파일 버전 올리기 단계로 이루어집니다.
 *
 * 스키마는 사람이 편집하는 데이터(`*.settings.xml`)입니다. 엔진 파일(`engine/settings/engine.settings.xml`) 위에 게임 파일(팩 설정의
 * `_userSettingsSchema`)을 덧붙입니다. 코드가 가진 것은 이름으로 고르는 핸들러 레지스트리(`UserSettingsHandlerRegistry`)뿐입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Resource/ResourceID.h"

namespace sw
{
    class GlobalVariableManager;
    class UserSettingsHandlerRegistry;
    class XMLNode;

    /** @brief 설정 값의 종류입니다. 값은 늘 정규화한 글(`UserSettingsSchema::normalizeValue`)로 다닙니다. */
    enum class UserSettingType : uint8
    {
        Bool,       ///< `true` · `false`
        Int,        ///< 정수입니다. min, max, step 이 있습니다.
        Float,      ///< 실수입니다. min, max, step 이 있습니다.
        Enum,       ///< 선택지 이름 하나(`<Option value=…>` 또는 `optionsFrom` 공급자)
        KeyBinding, ///< 입력 슬롯 글(`InputSlotUtil`). 빈 글은 "입력 맵의 기본 바인딩" 입니다.
        String,     ///< 자유 텍스트입니다.
    };

    /** @brief 설정 값을 사용자 파일(JSON)에 적는 모양입니다. */
    enum class UserSettingStorageKind : uint8
    {
        Bool,   ///< JSON 불입니다.
        Number, ///< JSON 숫자입니다. 숫자로 읽히지 않는 글은 글 그대로 적습니다.
        Text,   ///< JSON 글입니다.
    };

    /** @brief 값이 대상에 닿는 때입니다. */
    enum class UserSettingApplyTiming : uint8
    {
        Immediate,   ///< 보류 값을 넣는 즉시 대상에 미리 적용합니다(볼륨 슬라이더). `revertPending` 이 되돌립니다.
        OnConfirm,   ///< `applyPending` 때 적용합니다.
        NeedsRestart ///< `applyPending` 때 저장만 하고, 다음 실행의 기동 적용에서 닿습니다(`isRestartRequired`).
    };

    /** @brief 값을 받는 대상의 종류입니다. */
    enum class UserSettingTargetKind : uint8
    {
        None,           ///< 대상이 없습니다. 게임 코드가 `UserSettingsManager::getValue` 와 변경 통보로 읽습니다(난이도 등).
        GlobalVariable, ///< 전역 변수(`gv:gv_name`)입니다. `GlobalVariableInfo::setValueFromString` 으로 넣습니다.
        Handler,        ///< 이름 붙은 핸들러(`handler:name`)입니다. `UserSettingsHandlerRegistry` 에서 찾습니다.
    };

    /** @brief 의존 조건의 비교입니다. */
    enum class UserSettingConditionOperator : uint8
    {
        Equal,
        NotEqual,
    };

    /** @brief 사용자 파일 버전 올리기 단계의 동작입니다. */
    enum class UserSettingsUpgradeOperation : uint8
    {
        Rename,   ///< 키 이름을 바꿉니다(`key` → `to`).
        Remove,   ///< 키를 버립니다.
        MapValue, ///< 키의 값이 `from` 이면 `to` 로 바꿉니다(선택지 이름 바꾸기).
        Scale,    ///< 숫자 값에 `scale` 을 곱합니다(0~100 → 0~1).
    };
} // namespace sw

namespace sw
{
    /** @brief `enabledWhen` 조건 하나 — "설정 @p _settingID 의 값이 @p _value 와 같다(다르다)" 입니다. */
    struct UserSettingCondition
    {
        hashed_string                _settingID{};
        string                       _value{};
        UserSettingConditionOperator _operator{ UserSettingConditionOperator::Equal };
    };
} // namespace sw

namespace sw
{
    /** @brief 열거형 설정의 선택지 하나입니다. */
    struct UserSettingOption
    {
        string _value{};       ///< 저장하고 비교하는 이름입니다.
        string _textKey{};     ///< 메뉴에 보일 로컬라이제이션 키(비면 `_value` 를 그대로 보입니다)
        string _targetValue{}; ///< 전역 변수 대상에 넘길 글. 비면 선택지 순번(0 부터)입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 설정 하나의 정의입니다. 스키마 XML 의 `<Setting>` 한 줄입니다.
     * @details 값은 정규화한 텍스트로 다룬다. bool 은 `true`/`false`, 숫자는 가장 짧은 왕복 표기, 열거형은 선택지 이름, 키는 `InputSlotUtil` 텍스트다.
     */
    struct UserSettingDef
    {
        hashed_string                _id{};                   ///< `category.name` 형태의 id 입니다(사용자 파일의 키).
        hashed_string                _category{};             ///< 소속 카테고리 id
        string                       _textKey{};              ///< 메뉴 이름의 현지화 키입니다.
        string                       _descriptionKey{};       ///< 설명 로컬라이제이션 키(없으면 빈 글)
        string                       _defaultValue{};         ///< 정규화한 기본값입니다(게임 프리셋 덮어쓰기 전).
        vector<UserSettingOption>    _listOption{};           ///< 열거형 선택지(공급자를 쓰면 빈 목록)
        hashed_string                _optionProvider{};       ///< 런타임 선택지 공급자 이름(`optionsFrom`)
        hashed_string                _targetName{};           ///< 대상 이름(전역 변수 이름 또는 핸들러 이름)
        string                       _targetParam{};          ///< 핸들러에 넘기는 인자입니다(`param`, 예: 버스 이름, 액션 이름).
        vector<UserSettingCondition> _listEnabledCondition{}; ///< 모두 참일 때만 켜집니다(`enabledWhen`)
        hashed_string                _action{};               ///< 키 바인딩의 입력 맵 액션입니다.
        hashed_string                _context{};              ///< 키 바인딩: 겹침을 보는 범위(입력 맵 레이어). 비면 바인딩의 레이어입니다.
        float64                      _minValue{ 0.0 };        ///< 숫자의 아래 끝입니다.
        float64                      _maxValue{ 0.0 };        ///< 숫자의 위 끝입니다.
        float64                      _step{ 0.0 };            ///< 숫자의 눈금입니다(0 이면 연속).
        float32                      _confirmSeconds{ 0.0f }; ///< 0 보다 크면 바뀐 값을 그 시간 안에 확인하지 않으면 되돌립니다(화면 모드).
        uint32                       _bindIndex{ 0 };         ///< 키 바인딩에서 액션의 몇 번째 바인딩인지입니다.
        uint32                       _order{ 0 };             ///< 스키마에 적힌 순서(카테고리 안 표시 순서)
        UserSettingType              _type{ UserSettingType::Bool };
        UserSettingApplyTiming       _applyTiming{ UserSettingApplyTiming::OnConfirm };
        UserSettingTargetKind        _targetKind{ UserSettingTargetKind::None };
        uint8                        _platformMask{ 0xFF }; ///< `UserSettingsPlatform` 비트입니다. 그 플랫폼에서만 보이고 적용됩니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 설정 종류 하나의 성질입니다. 종류마다 다른 데이터(스키마 이름 · 파일 모양 · 전역 변수 대상 · 옵션 메뉴 행)는 이 표의 한 줄에서 읽습니다.
     * @details 표는 `UserSettingsSchema::getTypeTraits` 가 돌려줍니다. 종류를 더하면 열거자와 표 한 줄을 같이 더합니다 — 표 순서가 열거자 순서와
     *          어긋나면 컴파일이 멈춥니다. 값 정규화처럼 종류마다 코드가 다른 것은 각자의 switch 에 남고, UI 쪽 성질(행 문서 · 값 위젯 필드)은 그것을 그리는 옵션 메뉴(`OptionsMenuScreen`)가 갖습니다.
     */
    struct UserSettingTypeTraits
    {
        const utf8*            _pName;                  ///< 스키마 XML 의 `type` 값입니다.
        uint32                 _globalVariableTypeMask; ///< 대상으로 받을 수 있는 전역 변수 타입 비트(`SW_BIT( GlobalVariableType )`)입니다.
        UserSettingType        _type;
        UserSettingStorageKind _storageKind;
        bool                   _bNumeric; ///< min · max · step 이 있습니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 설정 카테고리(메뉴 탭) 하나입니다. */
    struct UserSettingCategoryDef
    {
        hashed_string _id{};
        string        _textKey{};
        uint32        _order{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 프리셋 하나가 정하는 값 하나입니다. */
    struct ScalabilityPresetValue
    {
        hashed_string _settingID{};
        string        _value{};
    };
} // namespace sw

namespace sw
{
    /** @brief 그래픽 품질 프리셋 하나(Low, Medium, High, Ultra)입니다. 묶인 설정마다 값 하나를 가집니다. */
    struct ScalabilityPresetDef
    {
        hashed_string                  _name{}; ///< 번들 설정(`graphics.quality`)의 선택지 이름입니다.
        vector<ScalabilityPresetValue> _listValue{};
    };
} // namespace sw

namespace sw
{
    /** @brief 첫 실행에 프리셋을 고르는 규칙 하나입니다. 조건을 모두 넘는 첫 규칙이 이깁니다. */
    struct ScalabilityAutoDetectRule
    {
        hashed_string _preset{};
        uint32        _minLogicalCoreCount{ 0 };
        uint32        _minSystemMemoryMb{ 0 };
        uint32        _minGPUMemoryMb{ 0 }; ///< 기본 어댑터의 전용 비디오 메모리 하한. 0 이면 보지 않는다. 메모리를 못 읽은 기계(0)는 하한이 있는 규칙에 맞지 않는다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 그래픽 품질 번들입니다(언리얼 Scalability, 유니티 Quality Settings 에 해당).
     * @details 번들 설정(`_settingID`, 열거형)을 프리셋 이름으로 바꾸면 묶인 설정들이 그 프리셋 값이 되고, 묶인 설정을 하나라도 프리셋과 다르게 바꾸면
     *          번들 설정이 `_customValue`("custom")가 된다.
     */
    struct ScalabilityGroupDef
    {
        hashed_string                     _settingID{};
        string                            _customValue{};
        vector<ScalabilityPresetDef>      _listPreset{};
        vector<ScalabilityAutoDetectRule> _listAutoDetectRule{};
        hashed_string                     _autoDetectFallback{}; ///< 어느 규칙에도 맞지 않을 때의 프리셋입니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 사용자 파일 버전 올리기 한 단계입니다. `_fromVersion` 버전 파일에 적용해 다음 버전으로 만듭니다. */
    struct UserSettingsUpgradeStep
    {
        string                       _key{};
        string                       _newKey{};
        string                       _fromValue{};
        string                       _toValue{};
        float64                      _scale{ 1.0 };
        uint32                       _fromVersion{ 0 };
        UserSettingsUpgradeOperation _operation{ UserSettingsUpgradeOperation::Rename };
    };
} // namespace sw

namespace sw
{
    /** @brief 플랫폼 비트입니다(`platforms="windows linux"`). */
    namespace UserSettingsPlatform
    {
        inline constexpr uint8 kWindows = SW_BIT( 0 );
        inline constexpr uint8 kLinux   = SW_BIT( 1 );
        inline constexpr uint8 kAll     = 0xFF;
        /** @brief 이 빌드의 플랫폼 비트입니다. */
        SW_API uint8 getCurrent();
    } // namespace UserSettingsPlatform
} // namespace sw

namespace sw
{
    /** @brief 값을 정규화한 결과입니다. */
    enum class UserSettingValueResult : uint8
    {
        Accepted, ///< 그대로 받았습니다.
        Clamped,  ///< 범위나 눈금으로 보정해 받았습니다.
        Rejected, ///< 형식이 틀리거나 선택지에 없습니다.
    };

    /**
     * @class UserSettingsSchema
     * @brief 엔진 스키마 위에 게임 스키마를 덧붙인 설정 정의 전체입니다.
     * @details 덧붙이기는 새 카테고리, 새 설정, 새 품질 번들, 새 버전 단계만 더한다. 이미 있는 id 를 다시 정의하면 로드 오류다.
     *          게임마다 다른 기본값은 팩 설정(`PackEngineSettings::_mapUserSettingDefault`)이 준다. 모르는 요소, 속성, 열거자 이름도 로드 오류다.
     */
    class SW_API UserSettingsSchema
    {
    public:
        UserSettingsSchema();

        /** @brief 모든 정의를 비웁니다. */
        void clear();

        /** @brief 리소스 경로의 스키마 XML 을 읽어 덧붙입니다. 실패하면 아무것도 더하지 않습니다. */
        [[nodiscard]] bool loadFromResource( const ResourceID& resourceID );
        /** @brief 스키마 XML 글을 읽어 덧붙입니다. 실패하면 아무것도 더하지 않습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );

        /**
         * @brief 정의 전체를 검사합니다. 범위, 기본값, 선택지, 의존 대상, 품질 번들, 핸들러와 전역 변수 이름을 확인합니다.
         * @param registry 핸들러와 선택지 공급자 이름을 대조할 레지스트리입니다.
         * @param pGlobalVariableManager 전역 변수 대상을 대조할 테이블입니다. nullptr 이면 전역 변수 검사를 건너뜁니다.
         * @return 문제가 하나라도 있으면 false 입니다(모두 오류로 알립니다).
         */
        [[nodiscard]] bool validate( const UserSettingsHandlerRegistry& registry, GlobalVariableManager* pGlobalVariableManager ) const;

        /**
         * @brief @p text 를 @p def 의 정규화한 텍스트로 바꿉니다. 숫자는 범위와 눈금에 맞춰 고칩니다(Clamped).
         * @details 선택지를 공급자에서 받는 열거형은 여기서 선택지를 보지 않는다(`UserSettingsManager` 가 공급자로 확인한다). 키 바인딩의 빈 텍스트는
         *          "기본 바인딩" 으로 받는다.
         */
        static UserSettingValueResult normalizeValue( const UserSettingDef& def, string_view text, string& outValue );

        /** @brief @p type 의 성질 표 한 줄입니다. */
        static const UserSettingTypeTraits& getTypeTraits( UserSettingType type );

        /** @brief 사용자 파일 버전 번호입니다(`version` 속성). */
        uint32                                 getVersion() const { return _version; }
        const vector<UserSettingCategoryDef>&  getCategories() const { return _listCategory; }
        const vector<UserSettingDef>&          getSettings() const { return _listSetting; }
        const vector<ScalabilityGroupDef>&     getScalabilityGroups() const { return _listScalabilityGroup; }
        const vector<UserSettingsUpgradeStep>& getUpgradeSteps() const { return _listUpgradeStep; }

        /** @brief id 로 설정을 찾습니다. 없으면 nullptr 입니다. */
        const UserSettingDef* findSetting( const hashed_string& id ) const;
        /** @brief id 로 설정의 순번(`getSettings` 안)을 찾습니다. 없으면 `invalid_index::kUint32` 입니다. */
        uint32 findSettingIndex( const hashed_string& id ) const;
        /** @brief id 로 카테고리를 찾습니다. 없으면 nullptr 입니다. */
        const UserSettingCategoryDef* findCategory( const hashed_string& id ) const;
        /** @brief @p settingID 가 번들 설정이거나 묶인 설정인 품질 번들입니다. 없으면 nullptr 입니다. */
        const ScalabilityGroupDef* findScalabilityGroupOf( const hashed_string& settingID ) const;

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
        void               rebuildIndex();

    private:
        vector<UserSettingCategoryDef>       _listCategory;
        vector<UserSettingDef>               _listSetting;
        vector<ScalabilityGroupDef>          _listScalabilityGroup;
        vector<UserSettingsUpgradeStep>      _listUpgradeStep;
        unordered_map<hashed_string, uint32> _mapSettingIndex; ///< id 에서 `_listSetting` 인덱스를 찾는 맵입니다.
        uint32                               _version;
    };
} // namespace sw
