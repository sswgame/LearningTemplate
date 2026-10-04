#include "pch.h"

#include "Engine/UserSettings/UserSettingsSchema.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/InputSlotUtil.h"
#include "Engine/UserSettings/UserSettingsRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettingsSchema" );

    namespace
    {
        struct UserSettingsSchemaInternal
        {
            /** @brief 키 바인딩 설정이 대상을 적지 않았을 때 쓰는 적용기입니다(입력 맵 리바인딩). */
            static constexpr utf8 kKeyBindingApplier[] = "input.keyBinding";

            /** @brief 이름 → 열거자 표의 한 줄입니다. */
            template <typename TEnum>
            struct NamedValue
            {
                const utf8* _pName;
                TEnum       _value;
            };

            static constexpr NamedValue<UserSettingType> kArrTypeName[] = {
                {      "bool",       UserSettingType::Bool},
                {       "int",        UserSettingType::Int},
                {     "float",      UserSettingType::Float},
                {      "enum",       UserSettingType::Enum},
                {"keyBinding", UserSettingType::KeyBinding},
                {    "string",     UserSettingType::String},
            };
            static constexpr NamedValue<UserSettingApplyTiming> kArrTimingName[] = {
                {"immediate",    UserSettingApplyTiming::Immediate},
                {  "confirm",    UserSettingApplyTiming::OnConfirm},
                {  "restart", UserSettingApplyTiming::NeedsRestart},
            };
            static constexpr NamedValue<UserSettingsUpgradeOperation> kArrUpgradeName[] = {
                {  "rename",   UserSettingsUpgradeOperation::Rename},
                {  "remove",   UserSettingsUpgradeOperation::Remove},
                {"mapValue", UserSettingsUpgradeOperation::MapValue},
                {   "scale",    UserSettingsUpgradeOperation::Scale},
            };
            static constexpr NamedValue<uint8> kArrPlatformName[] = {
                {"windows", UserSettingsPlatform::kWindows},
                {  "linux",   UserSettingsPlatform::kLinux},
            };

            static constexpr const utf8* kArrCategoryAttribute[]    = { "id", "text" };
            static constexpr const utf8* kArrSettingAttribute[]     = { "id", "category", "type", "default", "apply", "confirmSeconds", "target", "param",
                                                                        "text", "description", "min", "max", "step", "enabledWhen", "platforms", "action",
                                                                        "bindIndex", "context", "optionsFrom" };
            static constexpr const utf8* kArrOptionAttribute[]      = { "value", "text", "targetValue" };
            static constexpr const utf8* kArrScalabilityAttribute[] = { "setting", "custom", "autoDetectFallback" };
            static constexpr const utf8* kArrPresetAttribute[]      = { "name" };
            static constexpr const utf8* kArrValueAttribute[]       = { "setting", "value" };
            static constexpr const utf8* kArrAutoDetectAttribute[]  = { "preset", "minCores", "minMemoryMb" };
            static constexpr const utf8* kArrUpgradeAttribute[]     = { "version", "op", "key", "to", "valueFrom", "valueTo", "scale" };
            static constexpr const utf8* kArrRootAttribute[]        = { "version" };

            template <typename TEnum, size_t Count>
            static bool findNamedValue( const NamedValue<TEnum> ( &arrEntry )[Count], string_view name, TEnum& outValue )
            {
                for ( const NamedValue<TEnum>& entry : arrEntry )
                {
                    if ( StringUtil::equals( name, entry._pName, true ) )
                    {
                        outValue = entry._value;
                        return true;
                    }
                }
                return false;
            }

            template <size_t Count>
            static bool hasOnlyKnownAttributes( const XmlNode& node, const utf8* const ( &arrAllowed )[Count], string_view sourceName )
            {
                bool bAllKnown = true;
                for ( XmlAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    bool bKnown = false;
                    for ( const utf8* pAllowed : arrAllowed )
                        bKnown = bKnown || StringUtil::equals( attribute.getName(), pAllowed, true );
                    if ( bKnown == false )
                    {
                        SW_LOG_ERROR( "%#: <%#> has an unknown attribute '%#'", sourceName, node.getName(), attribute.getName() );
                        bAllKnown = false;
                    }
                }
                return bAllKnown;
            }

            /** @brief 실수 하나를 읽습니다. 속성이 없으면 @p fallback 이고 true, 글이 숫자가 아니면 오류를 알리고 false 입니다. */
            [[nodiscard]] static bool readNumberAttribute( const XmlNode& node, const utf8* pName, float64 fallback, float64& outValue, string_view sourceName )
            {
                const string_view text = node.getAttributeText( pName );
                outValue               = fallback;
                if ( text.empty() )
                    return true;
                if ( StringUtil::parseDouble( text, outValue ) )
                    return true;
                SW_LOG_ERROR( "%#: <%#> attribute '%#' is not a number: '%#'", sourceName, node.getName(), pName, text );
                return false;
            }

            /** @brief `gv:name` · `applier:name` 을 읽습니다. 비어 있으면 대상 없음입니다. */
            [[nodiscard]] static bool parseTarget( string_view text, UserSettingDef& inoutDef, string_view sourceName )
            {
                if ( text.empty() )
                {
                    inoutDef._targetKind = UserSettingTargetKind::None;
                    return true;
                }
                const size_t colonPos = text.find( ':' );
                if ( colonPos != string_view::npos && colonPos + 1 < text.size() )
                {
                    const string_view kind = text.substr( 0, colonPos );
                    const string_view name = text.substr( colonPos + 1 );
                    if ( StringUtil::equals( kind, "gv", true ) )
                    {
                        inoutDef._targetKind = UserSettingTargetKind::GlobalVariable;
                        inoutDef._targetName = hashed_string( name );
                        return true;
                    }
                    if ( StringUtil::equals( kind, "applier", true ) )
                    {
                        inoutDef._targetKind = UserSettingTargetKind::Applier;
                        inoutDef._targetName = hashed_string( name );
                        return true;
                    }
                }
                SW_LOG_ERROR( "%#: setting '%#' has a target '%#' that is neither 'gv:<name>' nor 'applier:<name>'", sourceName, inoutDef._id.c_str(), text );
                return false;
            }

            /** @brief `a=b;c!=d` 를 읽습니다. */
            [[nodiscard]] static bool parseConditions( string_view text, UserSettingDef& inoutDef, string_view sourceName )
            {
                bool   bOk   = true;
                size_t start = 0;
                while ( start <= text.size() && text.empty() == false )
                {
                    size_t end = text.find( ';', start );
                    if ( end == string_view::npos )
                        end = text.size();
                    const string_view part = StringUtil::trim( text.substr( start, end - start ) );
                    start                  = end + 1;
                    if ( part.empty() )
                        continue;

                    UserSettingCondition condition;
                    size_t               operatorPos = part.find( "!=" );
                    size_t               valuePos    = operatorPos + 2;
                    condition._operator              = UserSettingConditionOperator::NotEqual;
                    if ( operatorPos == string_view::npos )
                    {
                        operatorPos         = part.find( '=' );
                        valuePos            = operatorPos + 1;
                        condition._operator = UserSettingConditionOperator::Equal;
                    }
                    if ( operatorPos == string_view::npos || operatorPos == 0 )
                    {
                        SW_LOG_ERROR( "%#: setting '%#' has an enabledWhen part '%#' that is not 'id=value' or 'id!=value'", sourceName, inoutDef._id.c_str(), part );
                        bOk = false;
                        continue;
                    }
                    condition._settingId = hashed_string( StringUtil::trim( part.substr( 0, operatorPos ) ) );
                    condition._value     = string( StringUtil::trim( part.substr( valuePos ) ) );
                    inoutDef._listEnabledCondition.push_back( condition );
                }
                return bOk;
            }

            /** @brief `windows linux` 를 비트로 읽습니다. */
            [[nodiscard]] static bool parsePlatforms( string_view text, uint8& outMask, string_view sourceName, const hashed_string& settingId )
            {
                if ( text.empty() )
                {
                    outMask = UserSettingsPlatform::kAll;
                    return true;
                }
                outMask      = 0;
                bool   bOk   = true;
                size_t start = 0;
                while ( start < text.size() )
                {
                    size_t end = text.find_first_of( " ,", start );
                    if ( end == string_view::npos )
                        end = text.size();
                    const string_view name = text.substr( start, end - start );
                    start                  = end + 1;
                    if ( name.empty() )
                        continue;
                    uint8 bit = 0;
                    if ( findNamedValue( kArrPlatformName, name, bit ) == false )
                    {
                        SW_LOG_ERROR( "%#: setting '%#' names an unknown platform '%#'", sourceName, settingId.c_str(), name );
                        bOk = false;
                        continue;
                    }
                    outMask = static_cast<uint8>( outMask | bit );
                }
                return bOk;
            }

            /** @brief 숫자를 가장 짧은 왕복 표기로 적습니다. 실수는 float32 로 한 번 거쳐 `0.15000000000000002` 같은 꼬리를 없앱니다. */
            static string formatFloat( float64 value )
            {
                utf8 arrBuffer[constant::kMaxBuffer64]{};
                StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, static_cast<float32>( value ) );
                return string( arrBuffer );
            }

            static string formatInt( int64 value )
            {
                utf8 arrBuffer[constant::kMaxBuffer32]{};
                StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value );
                return string( arrBuffer );
            }

            /** @brief 숫자를 범위 · 눈금에 맞춥니다. 바뀌었으면 true 입니다. */
            static bool fitNumber( const UserSettingDef& def, float64& inoutValue )
            {
                const float64 original = inoutValue;
                if ( def._step > 0.0 )
                    inoutValue = def._minValue + MathUtil::round( ( inoutValue - def._minValue ) / def._step ) * def._step;
                inoutValue = MathUtil::clamp( inoutValue, def._minValue, def._maxValue );
                return MathUtil::abs( inoutValue - original ) > 1.0e-6;
            }

            static bool isNumeric( UserSettingType type ) { return type == UserSettingType::Int || type == UserSettingType::Float; }

            /** @brief 전역 변수 타입이 설정 종류의 값을 받을 수 있는지 봅니다. */
            static bool acceptsGlobalVariable( UserSettingType settingType, GlobalVariableType variableType )
            {
                switch ( settingType )
                {
                    case UserSettingType::Bool:
                        return variableType == GlobalVariableType::Boolean;
                    case UserSettingType::Int:
                        return variableType == GlobalVariableType::Int32 || variableType == GlobalVariableType::Enum;
                    case UserSettingType::Float:
                        return variableType == GlobalVariableType::Float;
                    case UserSettingType::Enum:
                        return variableType == GlobalVariableType::Int32 || variableType == GlobalVariableType::Enum || variableType == GlobalVariableType::String;
                    case UserSettingType::KeyBinding:
                        return false;
                    case UserSettingType::String:
                        return variableType == GlobalVariableType::String;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint8 UserSettingsPlatform::getCurrent()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return kWindows;
#elif defined( SW_PLATFORM_LINUX )
        return kLinux;
#else
        return kAll;
#endif
    }
} // namespace sw

namespace sw
{
    UserSettingsSchema::UserSettingsSchema()
        : _listCategory{}
        , _listSetting{}
        , _listScalabilityGroup{}
        , _listUpgradeStep{}
        , _mapSettingIndex{}
        , _version{ 1 }
    {
    }

    void UserSettingsSchema::clear()
    {
        _listCategory.clear();
        _listSetting.clear();
        _listScalabilityGroup.clear();
        _listUpgradeStep.clear();
        _mapSettingIndex.clear();
        _version = 1;
    }

    bool UserSettingsSchema::loadFromResource( string_view resourcePath )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( resourcePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to read the user settings schema '%#'", resourcePath );
            return false;
        }
        return loadRoot( doc.getRoot( "UserSettingsSchema" ), resourcePath );
    }

    bool UserSettingsSchema::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "Failed to parse the user settings schema '%#'", sourceName );
            return false;
        }
        return loadRoot( doc.getRoot( "UserSettingsSchema" ), sourceName );
    }

    bool UserSettingsSchema::loadRoot( const XmlNode& root, string_view sourceName )
    {
        using Internal = UserSettingsSchemaInternal;
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the root element is not <UserSettingsSchema>", sourceName );
            return false;
        }

        // 다 읽은 뒤에만 합친다 — 반쯤 읽은 덧붙이기가 남으면 다음 검사가 엉뚱한 줄을 탓한다.
        UserSettingsSchema overlay;
        bool               bOk = Internal::hasOnlyKnownAttributes( root, Internal::kArrRootAttribute, sourceName );
        int32              version{ 0 };
        if ( root.tryGetAttributeIntInRange( "version", 0, 0, 1 << 20, version ) == false )
            bOk = false;
        overlay._version = static_cast<uint32>( version );

        const uint32 orderBase = static_cast<uint32>( _listSetting.size() );
        for ( XmlNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, "Category", true ) )
            {
                bOk = Internal::hasOnlyKnownAttributes( child, Internal::kArrCategoryAttribute, sourceName ) && bOk;
                UserSettingCategoryDef category;
                category._id      = hashed_string( child.getAttributeText( "id" ) );
                category._textKey = string( child.getAttributeText( "text" ) );
                category._order   = static_cast<uint32>( _listCategory.size() + overlay._listCategory.size() );
                if ( category._id.empty() )
                {
                    SW_LOG_ERROR( "%#: <Category> without an id", sourceName );
                    bOk = false;
                    continue;
                }
                overlay._listCategory.push_back( category );
            }
            else if ( StringUtil::equals( pName, "Setting", true ) )
            {
                bOk = Internal::hasOnlyKnownAttributes( child, Internal::kArrSettingAttribute, sourceName ) && bOk;
                UserSettingDef def;
                def._id             = hashed_string( child.getAttributeText( "id" ) );
                def._category       = hashed_string( child.getAttributeText( "category" ) );
                def._textKey        = string( child.getAttributeText( "text" ) );
                def._descriptionKey = string( child.getAttributeText( "description" ) );
                def._defaultValue   = string( child.getAttributeText( "default" ) );
                def._targetParam    = string( child.getAttributeText( "param" ) );
                def._optionProvider = hashed_string( child.getAttributeText( "optionsFrom" ) );
                def._action         = hashed_string( child.getAttributeText( "action" ) );
                def._context        = hashed_string( child.getAttributeText( "context" ) );
                def._order          = orderBase + static_cast<uint32>( overlay._listSetting.size() );
                if ( def._id.empty() )
                {
                    SW_LOG_ERROR( "%#: <Setting> without an id", sourceName );
                    bOk = false;
                    continue;
                }

                if ( Internal::findNamedValue( Internal::kArrTypeName, child.getAttributeText( "type" ), def._type ) == false )
                {
                    SW_LOG_ERROR( "%#: setting '%#' has an unknown type '%#'", sourceName, def._id.c_str(), child.getAttributeText( "type" ) );
                    bOk = false;
                }
                const string_view timingText = child.getAttributeText( "apply" );
                if ( timingText.empty() == false && Internal::findNamedValue( Internal::kArrTimingName, timingText, def._applyTiming ) == false )
                {
                    SW_LOG_ERROR( "%#: setting '%#' has an unknown apply timing '%#'", sourceName, def._id.c_str(), timingText );
                    bOk = false;
                }
                bOk = Internal::parseTarget( child.getAttributeText( "target" ), def, sourceName ) && bOk;
                if ( def._type == UserSettingType::KeyBinding && def._targetKind == UserSettingTargetKind::None )
                {
                    def._targetKind = UserSettingTargetKind::Applier;
                    def._targetName = hashed_string( Internal::kKeyBindingApplier );
                }
                bOk = Internal::parseConditions( child.getAttributeText( "enabledWhen" ), def, sourceName ) && bOk;
                bOk = Internal::parsePlatforms( child.getAttributeText( "platforms" ), def._platformMask, sourceName, def._id ) && bOk;

                float64 confirmSeconds{ 0.0 };
                bOk                 = Internal::readNumberAttribute( child, "min", 0.0, def._minValue, sourceName ) && bOk;
                bOk                 = Internal::readNumberAttribute( child, "max", 0.0, def._maxValue, sourceName ) && bOk;
                bOk                 = Internal::readNumberAttribute( child, "step", 0.0, def._step, sourceName ) && bOk;
                bOk                 = Internal::readNumberAttribute( child, "confirmSeconds", 0.0, confirmSeconds, sourceName ) && bOk;
                def._confirmSeconds = static_cast<float32>( confirmSeconds );
                int32 bindIndex{ 0 };
                if ( child.tryGetAttributeIntInRange( "bindIndex", 0, 0, 15, bindIndex ) == false )
                    bOk = false;
                def._bindIndex = static_cast<uint32>( bindIndex );

                for ( XmlNode optionNode = child.findChild(); optionNode.isValid(); optionNode = optionNode.findNextSibling() )
                {
                    if ( StringUtil::equals( optionNode.getName(), "Option", true ) == false )
                    {
                        SW_LOG_ERROR( "%#: setting '%#' has an unknown child <%#>", sourceName, def._id.c_str(), optionNode.getName() );
                        bOk = false;
                        continue;
                    }
                    bOk = Internal::hasOnlyKnownAttributes( optionNode, Internal::kArrOptionAttribute, sourceName ) && bOk;
                    UserSettingOption option;
                    option._value       = string( optionNode.getAttributeText( "value" ) );
                    option._textKey     = string( optionNode.getAttributeText( "text" ) );
                    option._targetValue = string( optionNode.getAttributeText( "targetValue" ) );
                    def._listOption.push_back( option );
                }

                // 기본값을 정규화해 둔다(`0.50` → `0.5`). 범위 밖 · 형식 오류는 그대로 두어 검사(`validate`)가 알린다.
                string normalized;
                if ( normalizeValue( def, def._defaultValue, normalized ) == UserSettingValueResult::Accepted )
                    def._defaultValue = normalized;
                overlay._listSetting.push_back( def );
            }
            else if ( StringUtil::equals( pName, "Scalability", true ) )
            {
                bOk = Internal::hasOnlyKnownAttributes( child, Internal::kArrScalabilityAttribute, sourceName ) && bOk;
                ScalabilityGroupDef group;
                group._settingId          = hashed_string( child.getAttributeText( "setting" ) );
                group._customValue        = string( child.getAttributeText( "custom" ) );
                group._autoDetectFallback = hashed_string( child.getAttributeText( "autoDetectFallback" ) );
                for ( XmlNode groupChild = child.findChild(); groupChild.isValid(); groupChild = groupChild.findNextSibling() )
                {
                    if ( StringUtil::equals( groupChild.getName(), "Preset", true ) )
                    {
                        bOk = Internal::hasOnlyKnownAttributes( groupChild, Internal::kArrPresetAttribute, sourceName ) && bOk;
                        ScalabilityPresetDef preset;
                        preset._name = hashed_string( groupChild.getAttributeText( "name" ) );
                        for ( XmlNode valueNode = groupChild.findChild(); valueNode.isValid(); valueNode = valueNode.findNextSibling() )
                        {
                            if ( StringUtil::equals( valueNode.getName(), "Value", true ) == false )
                            {
                                SW_LOG_ERROR( "%#: preset '%#' has an unknown child <%#>", sourceName, preset._name.c_str(), valueNode.getName() );
                                bOk = false;
                                continue;
                            }
                            bOk = Internal::hasOnlyKnownAttributes( valueNode, Internal::kArrValueAttribute, sourceName ) && bOk;
                            preset._listValue.push_back(
                                ScalabilityPresetValue{ hashed_string( valueNode.getAttributeText( "setting" ) ), string( valueNode.getAttributeText( "value" ) ) } );
                        }
                        group._listPreset.push_back( preset );
                    }
                    else if ( StringUtil::equals( groupChild.getName(), "AutoDetect", true ) )
                    {
                        bOk = Internal::hasOnlyKnownAttributes( groupChild, Internal::kArrAutoDetectAttribute, sourceName ) && bOk;
                        int32 minCores{ 0 };
                        int32 minMemoryMb{ 0 };
                        bOk = groupChild.tryGetAttributeIntInRange( "minCores", 0, 0, 4096, minCores ) && bOk;
                        bOk = groupChild.tryGetAttributeIntInRange( "minMemoryMb", 0, 0, 1 << 30, minMemoryMb ) && bOk;
                        group._listAutoDetectRule.push_back( ScalabilityAutoDetectRule{ hashed_string( groupChild.getAttributeText( "preset" ) ),
                                                                                        static_cast<uint32>( minCores ), static_cast<uint32>( minMemoryMb ) } );
                    }
                    else
                    {
                        SW_LOG_ERROR( "%#: <Scalability> has an unknown child <%#>", sourceName, groupChild.getName() );
                        bOk = false;
                    }
                }
                overlay._listScalabilityGroup.push_back( group );
            }
            else if ( StringUtil::equals( pName, "Upgrade", true ) )
            {
                bOk = Internal::hasOnlyKnownAttributes( child, Internal::kArrUpgradeAttribute, sourceName ) && bOk;
                UserSettingsUpgradeStep step;
                int32                   fromVersion{ 0 };
                bOk               = child.tryGetAttributeIntInRange( "version", 0, 0, 1 << 20, fromVersion ) && bOk;
                step._fromVersion = static_cast<uint32>( fromVersion );
                step._key         = string( child.getAttributeText( "key" ) );
                step._newKey      = string( child.getAttributeText( "to" ) );
                step._fromValue   = string( child.getAttributeText( "valueFrom" ) );
                step._toValue     = string( child.getAttributeText( "valueTo" ) );
                bOk               = Internal::readNumberAttribute( child, "scale", 1.0, step._scale, sourceName ) && bOk;
                if ( Internal::findNamedValue( Internal::kArrUpgradeName, child.getAttributeText( "op" ), step._operation ) == false )
                {
                    SW_LOG_ERROR( "%#: <Upgrade> has an unknown op '%#'", sourceName, child.getAttributeText( "op" ) );
                    bOk = false;
                }
                if ( step._key.empty() )
                {
                    SW_LOG_ERROR( "%#: <Upgrade> without a key", sourceName );
                    bOk = false;
                }
                overlay._listUpgradeStep.push_back( step );
            }
            else
            {
                SW_LOG_ERROR( "%#: unknown element <%#>", sourceName, pName );
                bOk = false;
            }
        }

        // 겹치는 id 는 덧붙이기에서도 오류다 — 게임마다 다른 값은 기본값 덮어쓰기(게임 프리셋)로 준다.
        for ( const UserSettingDef& def : overlay._listSetting )
        {
            bool bDuplicate = findSetting( def._id ) != nullptr;
            for ( const UserSettingDef& other : overlay._listSetting )
                bDuplicate = bDuplicate || ( &other != &def && other._id == def._id );
            if ( bDuplicate )
            {
                SW_LOG_ERROR( "%#: setting '%#' is defined twice", sourceName, def._id.c_str() );
                bOk = false;
            }
        }
        for ( const UserSettingCategoryDef& category : overlay._listCategory )
        {
            if ( findCategory( category._id ) != nullptr )
            {
                SW_LOG_ERROR( "%#: category '%#' is defined twice", sourceName, category._id.c_str() );
                bOk = false;
            }
        }
        if ( bOk == false )
            return false;

        // 판 번호는 가장 큰 것을 쓴다 — 게임 덧붙이기가 자기 버전 단계를 더하면 그 판까지 올라간다.
        _version = _version > overlay._version ? _version : overlay._version;
        _listCategory.insert( _listCategory.end(), overlay._listCategory.begin(), overlay._listCategory.end() );
        _listSetting.insert( _listSetting.end(), overlay._listSetting.begin(), overlay._listSetting.end() );
        _listScalabilityGroup.insert( _listScalabilityGroup.end(), overlay._listScalabilityGroup.begin(), overlay._listScalabilityGroup.end() );
        _listUpgradeStep.insert( _listUpgradeStep.end(), overlay._listUpgradeStep.begin(), overlay._listUpgradeStep.end() );
        rebuildIndex();
        return true;
    }

    void UserSettingsSchema::rebuildIndex()
    {
        _mapSettingIndex.clear();
        for ( uint32 settingIndex = 0; settingIndex < static_cast<uint32>( _listSetting.size() ); ++settingIndex )
            _mapSettingIndex[_listSetting[settingIndex]._id] = settingIndex;
    }

    const UserSettingDef* UserSettingsSchema::findSetting( const hashed_string& id ) const
    {
        const uint32 settingIndex = findSettingIndex( id );
        return settingIndex != invalid_index::kUint32 ? &_listSetting[settingIndex] : nullptr;
    }

    uint32 UserSettingsSchema::findSettingIndex( const hashed_string& id ) const
    {
        const auto iter = _mapSettingIndex.find( id );
        return iter != _mapSettingIndex.end() ? iter->second : invalid_index::kUint32;
    }

    const UserSettingCategoryDef* UserSettingsSchema::findCategory( const hashed_string& id ) const
    {
        for ( const UserSettingCategoryDef& category : _listCategory )
        {
            if ( category._id == id )
                return &category;
        }
        return nullptr;
    }

    const ScalabilityGroupDef* UserSettingsSchema::findScalabilityGroupOf( const hashed_string& settingId ) const
    {
        for ( const ScalabilityGroupDef& group : _listScalabilityGroup )
        {
            if ( group._settingId == settingId )
                return &group;
            for ( const ScalabilityPresetDef& preset : group._listPreset )
            {
                for ( const ScalabilityPresetValue& value : preset._listValue )
                {
                    if ( value._settingId == settingId )
                        return &group;
                }
            }
        }
        return nullptr;
    }

    UserSettingValueResult UserSettingsSchema::normalizeValue( const UserSettingDef& def, string_view text, string& outValue )
    {
        using Internal          = UserSettingsSchemaInternal;
        const string_view value = StringUtil::trim( text );
        switch ( def._type )
        {
            case UserSettingType::Bool:
            {
                bool bValue{ false };
                if ( StringUtil::tryParseBool( value, bValue ) == false )
                    return UserSettingValueResult::Rejected;
                outValue = bValue ? "true" : "false";
                return UserSettingValueResult::Accepted;
            }
            case UserSettingType::Int:
            {
                float64 number{ 0.0 };
                if ( StringUtil::parseDouble( value, number ) == false )
                    return UserSettingValueResult::Rejected;
                const float64 rounded  = MathUtil::round( number );
                float64       fitted   = rounded;
                const bool    bClamped = Internal::fitNumber( def, fitted ) || MathUtil::abs( rounded - number ) > 1.0e-9;
                outValue               = Internal::formatInt( static_cast<int64>( MathUtil::round( fitted ) ) );
                return bClamped ? UserSettingValueResult::Clamped : UserSettingValueResult::Accepted;
            }
            case UserSettingType::Float:
            {
                float64 number{ 0.0 };
                if ( StringUtil::parseDouble( value, number ) == false )
                    return UserSettingValueResult::Rejected;
                const bool bClamped = Internal::fitNumber( def, number );
                outValue            = Internal::formatFloat( number );
                return bClamped ? UserSettingValueResult::Clamped : UserSettingValueResult::Accepted;
            }
            case UserSettingType::Enum:
            {
                if ( def._listOption.empty() )
                {
                    // 선택지를 공급자에서 받는 열거형 — 여기서는 형식만 본다(빈 글 = "자동").
                    outValue = string( value );
                    return UserSettingValueResult::Accepted;
                }
                for ( const UserSettingOption& option : def._listOption )
                {
                    if ( StringUtil::equals( value, option._value, true ) )
                    {
                        outValue = option._value;
                        return UserSettingValueResult::Accepted;
                    }
                }
                return UserSettingValueResult::Rejected;
            }
            case UserSettingType::KeyBinding:
            {
                if ( value.empty() )
                {
                    outValue.clear();
                    return UserSettingValueResult::Accepted;
                }
                InputSlot slot;
                if ( InputSlotUtil::tryParse( value, slot ) == false )
                    return UserSettingValueResult::Rejected;
                outValue = InputSlotUtil::toText( slot );
                return UserSettingValueResult::Accepted;
            }
            case UserSettingType::String:
            {
                outValue = string( value );
                return UserSettingValueResult::Accepted;
            }
        }
        return UserSettingValueResult::Rejected;
    }

    bool UserSettingsSchema::validate( const UserSettingApplierRegistry& registry, GlobalVariableManager* pGlobalVariableManager ) const
    {
        using Internal = UserSettingsSchemaInternal;
        bool bOk       = true;

        for ( const UserSettingDef& def : _listSetting )
        {
            const utf8* const pId = def._id.c_str();
            if ( findCategory( def._category ) == nullptr )
            {
                SW_LOG_ERROR( "setting '%#' names an unknown category '%#'", pId, def._category.c_str() );
                bOk = false;
            }
            if ( Internal::isNumeric( def._type ) )
            {
                const bool bRangeValid = def._minValue <= def._maxValue && def._step >= 0.0;
                if ( bRangeValid == false )
                {
                    SW_LOG_ERROR( "setting '%#' has an invalid range [%#, %#] step %#", pId, def._minValue, def._maxValue, def._step );
                    bOk = false;
                }
            }
            if ( def._type == UserSettingType::Enum )
            {
                const bool bHasProvider = def._optionProvider.empty() == false;
                if ( bHasProvider == false && def._listOption.empty() )
                {
                    SW_LOG_ERROR( "enum setting '%#' has neither <Option> children nor optionsFrom", pId );
                    bOk = false;
                }
                if ( bHasProvider && registry.hasOptionProvider( def._optionProvider ) == false )
                {
                    SW_LOG_ERROR( "setting '%#' names an unknown option provider '%#'", pId, def._optionProvider.c_str() );
                    bOk = false;
                }
                for ( size_t optionIndex = 0; optionIndex < def._listOption.size(); ++optionIndex )
                {
                    for ( size_t otherIndex = optionIndex + 1; otherIndex < def._listOption.size(); ++otherIndex )
                    {
                        if ( StringUtil::equals( def._listOption[optionIndex]._value, def._listOption[otherIndex]._value, true ) )
                        {
                            SW_LOG_ERROR( "setting '%#' lists option '%#' twice", pId, def._listOption[optionIndex]._value );
                            bOk = false;
                        }
                    }
                }
            }
            if ( def._type == UserSettingType::KeyBinding && def._action.empty() )
            {
                SW_LOG_ERROR( "key binding setting '%#' has no action", pId );
                bOk = false;
            }

            string normalized;
            if ( normalizeValue( def, def._defaultValue, normalized ) != UserSettingValueResult::Accepted )
            {
                SW_LOG_ERROR( "setting '%#' has a default '%#' that is not a valid value", pId, def._defaultValue );
                bOk = false;
            }

            switch ( def._targetKind )
            {
                case UserSettingTargetKind::None:
                {
                    break;
                }
                case UserSettingTargetKind::GlobalVariable:
                {
                    if ( pGlobalVariableManager == nullptr )
                        break;
                    const GlobalVariableInfo* pVariable = pGlobalVariableManager->findVariable( def._targetName.c_str() );
                    if ( pVariable == nullptr )
                    {
                        SW_LOG_ERROR( "setting '%#' targets an unknown global variable '%#'", pId, def._targetName.c_str() );
                        bOk = false;
                    }
                    else if ( Internal::acceptsGlobalVariable( def._type, pVariable->_type ) == false )
                    {
                        SW_LOG_ERROR( "setting '%#' cannot drive global variable '%#' (type mismatch)", pId, def._targetName.c_str() );
                        bOk = false;
                    }
                    break;
                }
                case UserSettingTargetKind::Applier:
                {
                    if ( registry.hasApplier( def._targetName ) == false )
                    {
                        SW_LOG_ERROR( "setting '%#' targets an unknown applier '%#'", pId, def._targetName.c_str() );
                        bOk = false;
                        break;
                    }
                    // 적용기가 받는 값을 아는 경우(창 방식 이름 · 해상도 형식) 기본값과 선택지를 지금 대조한다 — 실행 중에 처음 알면 늦다.
                    if ( registry.acceptsValue( def._targetName, def._defaultValue ) == false )
                    {
                        SW_LOG_ERROR( "setting '%#': applier '%#' does not accept the default '%#'", pId, def._targetName.c_str(), def._defaultValue );
                        bOk = false;
                    }
                    for ( const UserSettingOption& option : def._listOption )
                    {
                        if ( registry.acceptsValue( def._targetName, option._value ) == false )
                        {
                            SW_LOG_ERROR( "setting '%#': applier '%#' does not accept option '%#'", pId, def._targetName.c_str(), option._value );
                            bOk = false;
                        }
                    }
                    break;
                }
            }

            for ( const UserSettingCondition& condition : def._listEnabledCondition )
            {
                const UserSettingDef* pOther = findSetting( condition._settingId );
                if ( pOther == nullptr )
                {
                    SW_LOG_ERROR( "setting '%#' depends on an unknown setting '%#'", pId, condition._settingId.c_str() );
                    bOk = false;
                    continue;
                }
                if ( normalizeValue( *pOther, condition._value, normalized ) != UserSettingValueResult::Accepted )
                {
                    SW_LOG_ERROR( "setting '%#' depends on '%#' = '%#', which is not a valid value of it", pId, condition._settingId.c_str(), condition._value );
                    bOk = false;
                }
            }
        }

        for ( const ScalabilityGroupDef& group : _listScalabilityGroup )
        {
            const UserSettingDef* pGroupSetting = findSetting( group._settingId );
            if ( pGroupSetting == nullptr || pGroupSetting->_type != UserSettingType::Enum )
            {
                SW_LOG_ERROR( "<Scalability> setting '%#' is not an enum setting", group._settingId.c_str() );
                bOk = false;
                continue;
            }
            string normalized;
            if ( normalizeValue( *pGroupSetting, group._customValue, normalized ) != UserSettingValueResult::Accepted )
            {
                SW_LOG_ERROR( "<Scalability> '%#': custom value '%#' is not an option", group._settingId.c_str(), group._customValue );
                bOk = false;
            }
            // 선택지 하나하나(custom 밖)가 프리셋 하나다 — 프리셋이 없는 선택지를 고르면 묶인 설정이 아무것도 바뀌지 않는다.
            for ( const UserSettingOption& option : pGroupSetting->_listOption )
            {
                if ( StringUtil::equals( option._value, group._customValue, true ) )
                    continue;
                bool bHasPreset = false;
                for ( const ScalabilityPresetDef& preset : group._listPreset )
                    bHasPreset = bHasPreset || StringUtil::equals( preset._name.c_str(), option._value, true );
                if ( bHasPreset == false )
                {
                    SW_LOG_ERROR( "<Scalability> '%#': option '%#' has no <Preset>", group._settingId.c_str(), option._value );
                    bOk = false;
                }
            }
            for ( const ScalabilityPresetDef& preset : group._listPreset )
            {
                if ( normalizeValue( *pGroupSetting, preset._name.c_str(), normalized ) != UserSettingValueResult::Accepted )
                {
                    SW_LOG_ERROR( "<Scalability> '%#': preset '%#' is not an option", group._settingId.c_str(), preset._name.c_str() );
                    bOk = false;
                }
                for ( const ScalabilityPresetValue& value : preset._listValue )
                {
                    const UserSettingDef* pMember = findSetting( value._settingId );
                    if ( pMember == nullptr || pMember == pGroupSetting )
                    {
                        SW_LOG_ERROR( "<Scalability> preset '%#' sets an unknown setting '%#'", preset._name.c_str(), value._settingId.c_str() );
                        bOk = false;
                        continue;
                    }
                    if ( normalizeValue( *pMember, value._value, normalized ) != UserSettingValueResult::Accepted )
                    {
                        SW_LOG_ERROR( "<Scalability> preset '%#' sets '%#' to an invalid value '%#'", preset._name.c_str(), value._settingId.c_str(), value._value );
                        bOk = false;
                    }
                }
            }
            for ( const ScalabilityAutoDetectRule& rule : group._listAutoDetectRule )
            {
                if ( normalizeValue( *pGroupSetting, rule._preset.c_str(), normalized ) != UserSettingValueResult::Accepted )
                {
                    SW_LOG_ERROR( "<AutoDetect> names an unknown preset '%#'", rule._preset.c_str() );
                    bOk = false;
                }
            }
            if ( group._autoDetectFallback.empty() == false && normalizeValue( *pGroupSetting, group._autoDetectFallback.c_str(), normalized ) != UserSettingValueResult::Accepted )
            {
                SW_LOG_ERROR( "<Scalability> '%#': autoDetectFallback '%#' is not an option", group._settingId.c_str(), group._autoDetectFallback.c_str() );
                bOk = false;
            }
        }

        for ( const UserSettingsUpgradeStep& step : _listUpgradeStep )
        {
            if ( step._fromVersion >= _version )
            {
                SW_LOG_ERROR( "<Upgrade> from version %# is not older than the schema version %#", step._fromVersion, _version );
                bOk = false;
            }
        }
        return bOk;
    }
} // namespace sw
