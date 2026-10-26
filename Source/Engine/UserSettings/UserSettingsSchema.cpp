#include "pch.h"

#include "Engine/UserSettings/UserSettingsSchema.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/UserSettings/UserSettingsHandlerRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettingsSchema" );

    namespace
    {
        struct UserSettingsSchemaInternal
        {
            static constexpr int32 kMaxSchemaVersion = 1 << 20; ///< 판 번호의 상한(깨진 큰 수를 거른다)
            static constexpr int32 kMaxBindIndex     = 15;      ///< 액션 하나의 바인딩 번호 상한
            static constexpr int32 kMaxCoreCount     = 4096;    ///< 자동 감지 규칙의 코어 수 상한
            static constexpr int32 kMaxMemoryMb      = 1 << 30; ///< 자동 감지 규칙의 메모리(MB) 상한

            /** @brief 키 바인딩 설정이 대상을 적지 않았을 때 쓰는 핸들러입니다(입력 맵 리바인딩). */
            static constexpr utf8 kKeyBindingHandler[] = "input.keyBinding";

            static constexpr uint32 kBooleanVariable = SW_BIT( static_cast<uint32>( GlobalVariableType::Boolean ) );
            static constexpr uint32 kInt32Variable   = SW_BIT( static_cast<uint32>( GlobalVariableType::Int32 ) );
            static constexpr uint32 kFloatVariable   = SW_BIT( static_cast<uint32>( GlobalVariableType::Float ) );
            static constexpr uint32 kStringVariable  = SW_BIT( static_cast<uint32>( GlobalVariableType::String ) );
            static constexpr uint32 kEnumVariable    = SW_BIT( static_cast<uint32>( GlobalVariableType::Enum ) );

            /** @brief 설정 종류의 성질 표입니다. 열거자 순서대로 한 줄씩 둡니다 — 아래 static_assert 가 순서와 줄 수를 지킵니다. */
            static constexpr UserSettingTypeTraits kArrTypeTraits[] = {
                {      "bool",                                                  kBooleanVariable,       UserSettingType::Bool,   UserSettingStorageKind::Bool, false},
                {       "int",                                    kInt32Variable | kEnumVariable,        UserSettingType::Int, UserSettingStorageKind::Number,  true},
                {     "float",                                                    kFloatVariable,      UserSettingType::Float, UserSettingStorageKind::Number,  true},
                {      "enum", kInt32Variable | kEnumVariable | kStringVariable | kFloatVariable,       UserSettingType::Enum,   UserSettingStorageKind::Text, false},
                {"keyBinding",                                                                 0, UserSettingType::KeyBinding,   UserSettingStorageKind::Text, false},
                {    "string",                                                   kStringVariable,     UserSettingType::String,   UserSettingStorageKind::Text, false},
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
            static constexpr const utf8* kArrAutoDetectAttribute[]  = { "preset", "minCores", "minMemoryMb", "minGPUMemoryMb" };
            static constexpr const utf8* kArrUpgradeAttribute[]     = { "version", "op", "key", "to", "valueFrom", "valueTo", "scale" };
            static constexpr const utf8* kArrRootAttribute[]        = { "version" };

            /**
             * @brief 실수 하나를 읽습니다. 속성이 없으면 @p inoutValue 를 두고 true 입니다(기본값은 정의 구조체의 초기값 — 여기서 정하지 않는다).
             * @details 텍스트가 숫자가 아니면 오류를 알리고 false 이며 @p inoutValue 는 그대로입니다.
             */
            [[nodiscard]] static bool readNumberAttribute( const XMLNode& node, const utf8* pName, float64& inoutValue, string_view sourceName )
            {
                const string_view text = node.getAttributeText( pName );
                if ( text.empty() )
                    return true;
                float64 value{};
                if ( StringUtil::parseDouble( text, value ) )
                {
                    inoutValue = value;
                    return true;
                }
                SW_LOG_ERROR( "%#: <%#> attribute '%#' is not a number: '%#'", sourceName, node.getName(), pName, text );
                return false;
            }

            /** @brief `gv:name` 과 `handler:name` 을 읽습니다. 비어 있으면 대상 없음입니다. */
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
                    if ( StringUtil::equals( kind, "gv" ) )
                    {
                        inoutDef._targetKind = UserSettingTargetKind::GlobalVariable;
                        inoutDef._targetName = hashed_string( name );
                        return true;
                    }
                    if ( StringUtil::equals( kind, "handler" ) )
                    {
                        inoutDef._targetKind = UserSettingTargetKind::Handler;
                        inoutDef._targetName = hashed_string( name );
                        return true;
                    }
                }
                SW_LOG_ERROR( "%#: setting '%#' has a target '%#' that is neither 'gv:<name>' nor 'handler:<name>'", sourceName, inoutDef._id.c_str(), text );
                return false;
            }

            /** @brief `a=b;c!=d` 를 읽습니다. */
            [[nodiscard]] static bool parseConditions( string_view text, UserSettingDef& inoutDef, string_view sourceName )
            {
                bool bOk = true;
                for ( const string_view part : StringUtil::split( text, ";", { ._bTrim = true, ._bDropEmpty = true } ) )
                {
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
                    condition._settingID = hashed_string( StringUtil::trim( part.substr( 0, operatorPos ) ) );
                    condition._value     = string( StringUtil::trim( part.substr( valuePos ) ) );
                    inoutDef._listEnabledCondition.push_back( condition );
                }
                return bOk;
            }

            /** @brief `windows linux` 를 비트로 읽습니다. */
            [[nodiscard]] static bool parsePlatforms( string_view text, uint8& outMask, string_view sourceName, const hashed_string& settingID )
            {
                if ( text.empty() )
                {
                    outMask = UserSettingsPlatform::kAll;
                    return true;
                }
                outMask  = 0;
                bool bOk = true;
                StringUtil::forEachToken( text, " ,", [&]( string_view name )
                {
                    uint8 bit = 0;
                    if ( NamedValueUtil::find( kArrPlatformName, name, bit ) == false )
                    {
                        SW_LOG_ERROR( "%#: setting '%#' names an unknown platform '%#'", sourceName, settingID.c_str(), name );
                        bOk = false;
                        return;
                    }
                    outMask = static_cast<uint8>( outMask | bit );
                } );
                return bOk;
            }

            /** @brief 숫자를 가장 짧은 왕복 표기로 적습니다. 실수는 float32 를 한 번 거쳐 `0.15000000000000002` 같은 꼬리를 없앱니다. */
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

            /** @brief 숫자를 범위와 눈금에 맞춥니다. 바뀌었으면 true 입니다. */
            static bool fitNumber( const UserSettingDef& def, float64& inoutValue )
            {
                const float64 original = inoutValue;
                if ( def._step > 0.0 )
                    inoutValue = def._minValue + MathUtil::round( ( inoutValue - def._minValue ) / def._step ) * def._step;
                inoutValue = MathUtil::clamp( inoutValue, def._minValue, def._maxValue );
                return MathUtil::abs( inoutValue - original ) > 1.0e-6;
            }

            /** @brief 스키마 XML 의 `type` 값(철자 그대로)으로 설정 종류를 찾습니다. */
            static bool findTypeByName( string_view name, UserSettingType& outType )
            {
                for ( const UserSettingTypeTraits& traits : kArrTypeTraits )
                {
                    if ( StringUtil::equals( name, traits._pName ) )
                    {
                        outType = traits._type;
                        return true;
                    }
                }
                return false;
            }

            /** @brief 표 순서가 열거자 순서와 같은지 봅니다(`getTypeTraits` 가 열거자 값으로 줄을 고른다). */
            static constexpr bool isTypeTraitsInEnumOrder()
            {
                for ( size_t index = 0; index < std::size( kArrTypeTraits ); ++index )
                {
                    if ( static_cast<size_t>( kArrTypeTraits[index]._type ) != index )
                        return false;
                }
                return true;
            }

            // ------------------------------------------------------------------------------
            // 스키마 읽기 — `loadRoot` 가 요소마다 부른다. 반환값은 "오류 없음"이다(false 여도 읽기는 이어 가 모든 오류를 로그로 보인다).
            // ------------------------------------------------------------------------------

            /** @brief `<Category>` 하나를 읽습니다. id 가 비면 false 이며 @p outCategory 의 id 가 비어 있어 호출한 쪽이 목록에 넣지 않습니다. */
            [[nodiscard]] static bool readCategory( const XMLNode& node, uint32 order, string_view sourceName, UserSettingCategoryDef& outCategory )
            {
                const bool bKnownAttributes = XMLNameCheck::reportUnknownAttributes( node, kArrCategoryAttribute, sourceName );
                outCategory._id             = hashed_string( node.getAttributeText( "id" ) );
                outCategory._textKey        = string( node.getAttributeText( "text" ) );
                outCategory._order          = order;
                if ( outCategory._id.empty() )
                {
                    SW_LOG_ERROR( "%#: <Category> without an id", sourceName );
                    return false;
                }
                return bKnownAttributes;
            }

            /** @brief `<Setting>` 아래 `<Option>` 들을 읽습니다. 다른 자식은 오류입니다. */
            [[nodiscard]] static bool readOptions( const XMLNode& node, string_view sourceName, UserSettingDef& inoutDef )
            {
                bool bOk = true;
                for ( XMLNode optionNode = node.findChild(); optionNode.isValid(); optionNode = optionNode.findNextSibling() )
                {
                    if ( StringUtil::equals( optionNode.getName(), "Option", true ) == false )
                    {
                        SW_LOG_ERROR( "%#: setting '%#' has an unknown child <%#>", sourceName, inoutDef._id.c_str(), optionNode.getName() );
                        bOk = false;
                        continue;
                    }
                    bOk = XMLNameCheck::reportUnknownAttributes( optionNode, kArrOptionAttribute, sourceName ) && bOk;
                    UserSettingOption option;
                    option._value       = string( optionNode.getAttributeText( "value" ) );
                    option._textKey     = string( optionNode.getAttributeText( "text" ) );
                    option._targetValue = string( optionNode.getAttributeText( "targetValue" ) );
                    inoutDef._listOption.push_back( option );
                }
                return bOk;
            }

            /** @brief 설정의 숫자 칸(min · max · step · confirmSeconds · bindIndex)을 읽습니다. 적지 않은 칸은 UserSettingDef 의 초기값입니다. */
            [[nodiscard]] static bool readNumberFields( const XMLNode& node, string_view sourceName, UserSettingDef& inoutDef )
            {
                float64 confirmSeconds   = static_cast<float64>( inoutDef._confirmSeconds );
                bool    bOk              = readNumberAttribute( node, "min", inoutDef._minValue, sourceName );
                bOk                      = readNumberAttribute( node, "max", inoutDef._maxValue, sourceName ) && bOk;
                bOk                      = readNumberAttribute( node, "step", inoutDef._step, sourceName ) && bOk;
                bOk                      = readNumberAttribute( node, "confirmSeconds", confirmSeconds, sourceName ) && bOk;
                inoutDef._confirmSeconds = static_cast<float32>( confirmSeconds );
                int32 bindIndex          = static_cast<int32>( inoutDef._bindIndex );
                if ( node.readOptionalAttributeInRange( "bindIndex", 0, kMaxBindIndex, bindIndex ) == false )
                    bOk = false;
                inoutDef._bindIndex = static_cast<uint32>( bindIndex );
                return bOk;
            }

            /** @brief `<Setting>` 하나를 읽습니다. id 가 비면 false 이며 @p outDef 는 목록에 넣지 않습니다. 기본값은 정규화해 둡니다. */
            [[nodiscard]] static bool readSetting( const XMLNode& node, uint32 order, string_view sourceName, UserSettingDef& outDef )
            {
                bool bOk               = XMLNameCheck::reportUnknownAttributes( node, kArrSettingAttribute, sourceName );
                outDef._id             = hashed_string( node.getAttributeText( "id" ) );
                outDef._category       = hashed_string( node.getAttributeText( "category" ) );
                outDef._textKey        = string( node.getAttributeText( "text" ) );
                outDef._descriptionKey = string( node.getAttributeText( "description" ) );
                outDef._defaultValue   = string( node.getAttributeText( "default" ) );
                outDef._targetParam    = string( node.getAttributeText( "param" ) );
                outDef._optionProvider = hashed_string( node.getAttributeText( "optionsFrom" ) );
                outDef._action         = hashed_string( node.getAttributeText( "action" ) );
                outDef._context        = hashed_string( node.getAttributeText( "context" ) );
                outDef._order          = order;
                if ( outDef._id.empty() )
                {
                    SW_LOG_ERROR( "%#: <Setting> without an id", sourceName );
                    return false;
                }

                if ( findTypeByName( node.getAttributeText( "type" ), outDef._type ) == false )
                {
                    SW_LOG_ERROR( "%#: setting '%#' has an unknown type '%#'", sourceName, outDef._id.c_str(), node.getAttributeText( "type" ) );
                    bOk = false;
                }
                const string_view timingText = node.getAttributeText( "apply" );
                if ( timingText.empty() == false && NamedValueUtil::find( kArrTimingName, timingText, outDef._applyTiming ) == false )
                {
                    SW_LOG_ERROR( "%#: setting '%#' has an unknown apply timing '%#'", sourceName, outDef._id.c_str(), timingText );
                    bOk = false;
                }
                bOk = parseTarget( node.getAttributeText( "target" ), outDef, sourceName ) && bOk;
                if ( outDef._type == UserSettingType::KeyBinding && outDef._targetKind == UserSettingTargetKind::None )
                {
                    outDef._targetKind = UserSettingTargetKind::Handler;
                    outDef._targetName = hashed_string( kKeyBindingHandler );
                }
                bOk = parseConditions( node.getAttributeText( "enabledWhen" ), outDef, sourceName ) && bOk;
                bOk = parsePlatforms( node.getAttributeText( "platforms" ), outDef._platformMask, sourceName, outDef._id ) && bOk;
                bOk = readNumberFields( node, sourceName, outDef ) && bOk;
                bOk = readOptions( node, sourceName, outDef ) && bOk;

                // 기본값을 정규화해 둔다(`0.50` → `0.5`). 범위 밖이나 형식 오류는 그대로 두어 검사(`validate`)가 알린다.
                string normalized;
                if ( UserSettingsSchema::normalizeValue( outDef, outDef._defaultValue, normalized ) == UserSettingValueResult::Accepted )
                    outDef._defaultValue = normalized;
                return bOk;
            }

            /** @brief `<Scalability>` 아래 `<Preset>` 하나를 읽습니다. */
            [[nodiscard]] static bool readPreset( const XMLNode& node, string_view sourceName, ScalabilityPresetDef& outPreset )
            {
                bool bOk        = XMLNameCheck::reportUnknownAttributes( node, kArrPresetAttribute, sourceName );
                outPreset._name = hashed_string( node.getAttributeText( "name" ) );
                for ( XMLNode valueNode = node.findChild(); valueNode.isValid(); valueNode = valueNode.findNextSibling() )
                {
                    if ( StringUtil::equals( valueNode.getName(), "Value", true ) == false )
                    {
                        SW_LOG_ERROR( "%#: preset '%#' has an unknown child <%#>", sourceName, outPreset._name.c_str(), valueNode.getName() );
                        bOk = false;
                        continue;
                    }
                    bOk = XMLNameCheck::reportUnknownAttributes( valueNode, kArrValueAttribute, sourceName ) && bOk;
                    outPreset._listValue.push_back( ScalabilityPresetValue{ hashed_string( valueNode.getAttributeText( "setting" ) ), string( valueNode.getAttributeText( "value" ) ) } );
                }
                return bOk;
            }

            /** @brief `<Scalability>` 아래 `<AutoDetect>` 하나를 읽어 규칙 목록에 더합니다. */
            [[nodiscard]] static bool readAutoDetect( const XMLNode& node, string_view sourceName, ScalabilityGroupDef& inoutGroup )
            {
                bool  bOk = XMLNameCheck::reportUnknownAttributes( node, kArrAutoDetectAttribute, sourceName );
                int32 minCores{ 0 };
                int32 minMemoryMb{ 0 };
                int32 minGPUMemoryMb{ 0 };
                bOk = node.readOptionalAttributeInRange( "minCores", 0, kMaxCoreCount, minCores ) && bOk;
                bOk = node.readOptionalAttributeInRange( "minMemoryMb", 0, kMaxMemoryMb, minMemoryMb ) && bOk;
                bOk = node.readOptionalAttributeInRange( "minGPUMemoryMb", 0, kMaxMemoryMb, minGPUMemoryMb ) && bOk;
                inoutGroup._listAutoDetectRule.push_back( ScalabilityAutoDetectRule{ hashed_string( node.getAttributeText( "preset" ) ), static_cast<uint32>( minCores ),
                                                                                     static_cast<uint32>( minMemoryMb ), static_cast<uint32>( minGPUMemoryMb ) } );
                return bOk;
            }

            /** @brief `<Scalability>` 하나(`<Preset>` · `<AutoDetect>` 자식 포함)를 읽습니다. */
            [[nodiscard]] static bool readScalability( const XMLNode& node, string_view sourceName, ScalabilityGroupDef& outGroup )
            {
                bool bOk                     = XMLNameCheck::reportUnknownAttributes( node, kArrScalabilityAttribute, sourceName );
                outGroup._settingID          = hashed_string( node.getAttributeText( "setting" ) );
                outGroup._customValue        = string( node.getAttributeText( "custom" ) );
                outGroup._autoDetectFallback = hashed_string( node.getAttributeText( "autoDetectFallback" ) );
                for ( XMLNode groupChild = node.findChild(); groupChild.isValid(); groupChild = groupChild.findNextSibling() )
                {
                    if ( StringUtil::equals( groupChild.getName(), "Preset", true ) )
                    {
                        ScalabilityPresetDef preset;
                        bOk = readPreset( groupChild, sourceName, preset ) && bOk;
                        outGroup._listPreset.push_back( preset );
                    }
                    else if ( StringUtil::equals( groupChild.getName(), "AutoDetect", true ) )
                    {
                        bOk = readAutoDetect( groupChild, sourceName, outGroup ) && bOk;
                    }
                    else
                    {
                        SW_LOG_ERROR( "%#: <Scalability> has an unknown child <%#>", sourceName, groupChild.getName() );
                        bOk = false;
                    }
                }
                return bOk;
            }

            /** @brief `<Upgrade>` 하나를 읽습니다. */
            [[nodiscard]] static bool readUpgrade( const XMLNode& node, string_view sourceName, UserSettingsUpgradeStep& outStep )
            {
                bool  bOk = XMLNameCheck::reportUnknownAttributes( node, kArrUpgradeAttribute, sourceName );
                int32 fromVersion{ 0 };
                bOk                  = node.readOptionalAttributeInRange( "version", 0, kMaxSchemaVersion, fromVersion ) && bOk;
                outStep._fromVersion = static_cast<uint32>( fromVersion );
                outStep._key         = string( node.getAttributeText( "key" ) );
                outStep._newKey      = string( node.getAttributeText( "to" ) );
                outStep._fromValue   = string( node.getAttributeText( "valueFrom" ) );
                outStep._toValue     = string( node.getAttributeText( "valueTo" ) );
                bOk                  = readNumberAttribute( node, "scale", outStep._scale, sourceName ) && bOk;
                if ( NamedValueUtil::find( kArrUpgradeName, node.getAttributeText( "op" ), outStep._operation ) == false )
                {
                    SW_LOG_ERROR( "%#: <Upgrade> has an unknown op '%#'", sourceName, node.getAttributeText( "op" ) );
                    bOk = false;
                }
                if ( outStep._key.empty() )
                {
                    SW_LOG_ERROR( "%#: <Upgrade> without a key", sourceName );
                    bOk = false;
                }
                return bOk;
            }

            // ------------------------------------------------------------------------------
            // 스키마 검사 — `validate` 가 설정 · 품질 번들마다 부른다. 반환값은 "문제 없음"이다(문제가 있어도 이어 가 모두 알린다).
            // ------------------------------------------------------------------------------

            /** @brief 열거형 설정의 선택지(공급자나 `<Option>` 이 있는지, 공급자 이름, 겹침)를 검사합니다. */
            [[nodiscard]] static bool validateEnumSetting( const UserSettingDef& def, const UserSettingsHandlerRegistry& registry )
            {
                bool              bOk          = true;
                const utf8* const pID          = def._id.c_str();
                const bool        bHasProvider = def._optionProvider.empty() == false;
                if ( bHasProvider == false && def._listOption.empty() )
                {
                    SW_LOG_ERROR( "enum setting '%#' has neither <Option> children nor optionsFrom", pID );
                    bOk = false;
                }
                if ( bHasProvider && registry.hasOptionProvider( def._optionProvider ) == false )
                {
                    SW_LOG_ERROR( "setting '%#' names an unknown option provider '%#'", pID, def._optionProvider.c_str() );
                    bOk = false;
                }
                for ( size_t optionIndex = 0; optionIndex < def._listOption.size(); ++optionIndex )
                {
                    for ( size_t otherIndex = optionIndex + 1; otherIndex < def._listOption.size(); ++otherIndex )
                    {
                        if ( StringUtil::equals( def._listOption[optionIndex]._value, def._listOption[otherIndex]._value ) )
                        {
                            SW_LOG_ERROR( "setting '%#' lists option '%#' twice", pID, def._listOption[optionIndex]._value );
                            bOk = false;
                        }
                    }
                }
                return bOk;
            }

            /** @brief 전역 변수 대상(이름, 종류 일치, 실수 변수를 모는 열거형의 targetValue)을 검사합니다. @p pGlobalVariableManager 가 없으면 건너뜁니다. */
            [[nodiscard]] static bool validateGlobalVariableTarget( const UserSettingDef& def, GlobalVariableManager* pGlobalVariableManager )
            {
                if ( pGlobalVariableManager == nullptr )
                    return true;
                const utf8* const         pID       = def._id.c_str();
                const GlobalVariableInfo* pVariable = pGlobalVariableManager->findVariable( def._targetName.c_str() );
                if ( pVariable == nullptr )
                {
                    SW_LOG_ERROR( "setting '%#' targets an unknown global variable '%#'", pID, def._targetName.c_str() );
                    return false;
                }
                if ( ( UserSettingsSchema::getTypeTraits( def._type )._globalVariableTypeMask & SW_BIT( static_cast<uint32>( pVariable->_type ) ) ) == 0 )
                {
                    SW_LOG_ERROR( "setting '%#' cannot drive global variable '%#' (type mismatch)", pID, def._targetName.c_str() );
                    return false;
                }
                bool bOk = true;
                if ( def._type == UserSettingType::Enum && pVariable->_type == GlobalVariableType::Float )
                {
                    // 실수 변수는 선택지 순번으로 채울 수 없다 — 모든 선택지가 수인 targetValue 를 가져야 한다(순번으로 떨어지는 길이 없다).
                    for ( const UserSettingOption& option : def._listOption )
                    {
                        float32 parsed{ 0.0f };
                        if ( StringUtil::parseFloat( option._targetValue, parsed ) == false )
                        {
                            SW_LOG_ERROR( "setting '%#' drives float variable '%#' - option '%#' needs a numeric targetValue", pID, def._targetName.c_str(), option._value.c_str() );
                            bOk = false;
                        }
                    }
                }
                return bOk;
            }

            /** @brief 핸들러 대상(이름, 기본값과 선택지를 핸들러가 받는지)을 검사합니다. 핸들러가 받는 값을 아는 경우에는 지금 대조한다. 실행 중에 처음 알면 늦다. */
            [[nodiscard]] static bool validateHandlerTarget( const UserSettingDef& def, const UserSettingsHandlerRegistry& registry )
            {
                const utf8* const pID = def._id.c_str();
                if ( registry.hasHandler( def._targetName ) == false )
                {
                    SW_LOG_ERROR( "setting '%#' targets an unknown handler '%#'", pID, def._targetName.c_str() );
                    return false;
                }
                bool bOk = true;
                if ( registry.acceptsValue( def._targetName, def._defaultValue ) == false )
                {
                    SW_LOG_ERROR( "setting '%#': handler '%#' does not accept the default '%#'", pID, def._targetName.c_str(), def._defaultValue );
                    bOk = false;
                }
                for ( const UserSettingOption& option : def._listOption )
                {
                    if ( registry.acceptsValue( def._targetName, option._value ) == false )
                    {
                        SW_LOG_ERROR( "setting '%#': handler '%#' does not accept option '%#'", pID, def._targetName.c_str(), option._value );
                        bOk = false;
                    }
                }
                return bOk;
            }

            /** @brief `enabledWhen` 조건이 가리키는 설정이 있고 그 값이 그 설정의 올바른 값인지 검사합니다. */
            [[nodiscard]] static bool validateConditions( const UserSettingsSchema& schema, const UserSettingDef& def )
            {
                bool   bOk = true;
                string normalized;
                for ( const UserSettingCondition& condition : def._listEnabledCondition )
                {
                    const UserSettingDef* pOther = schema.findSetting( condition._settingID );
                    if ( pOther == nullptr )
                    {
                        SW_LOG_ERROR( "setting '%#' depends on an unknown setting '%#'", def._id.c_str(), condition._settingID.c_str() );
                        bOk = false;
                        continue;
                    }
                    if ( UserSettingsSchema::normalizeValue( *pOther, condition._value, normalized ) != UserSettingValueResult::Accepted )
                    {
                        SW_LOG_ERROR( "setting '%#' depends on '%#' = '%#', which is not a valid value of it", def._id.c_str(), condition._settingID.c_str(), condition._value );
                        bOk = false;
                    }
                }
                return bOk;
            }

            /** @brief 설정 하나(카테고리, 범위, 선택지, 키 바인딩 액션, 기본값, 대상, 의존 조건)를 검사합니다. */
            [[nodiscard]] static bool validateSetting( const UserSettingsSchema& schema, const UserSettingDef& def, const UserSettingsHandlerRegistry& registry,
                                                       GlobalVariableManager* pGlobalVariableManager )
            {
                bool              bOk = true;
                const utf8* const pID = def._id.c_str();
                if ( schema.findCategory( def._category ) == nullptr )
                {
                    SW_LOG_ERROR( "setting '%#' names an unknown category '%#'", pID, def._category.c_str() );
                    bOk = false;
                }
                if ( UserSettingsSchema::getTypeTraits( def._type )._bNumeric )
                {
                    const bool bRangeValid = def._minValue <= def._maxValue && def._step >= 0.0;
                    if ( bRangeValid == false )
                    {
                        SW_LOG_ERROR( "setting '%#' has an invalid range [%#, %#] step %#", pID, def._minValue, def._maxValue, def._step );
                        bOk = false;
                    }
                }
                if ( def._type == UserSettingType::Enum )
                    bOk = validateEnumSetting( def, registry ) && bOk;
                if ( def._type == UserSettingType::KeyBinding && def._action.empty() )
                {
                    SW_LOG_ERROR( "key binding setting '%#' has no action", pID );
                    bOk = false;
                }

                string normalized;
                if ( UserSettingsSchema::normalizeValue( def, def._defaultValue, normalized ) != UserSettingValueResult::Accepted )
                {
                    SW_LOG_ERROR( "setting '%#' has a default '%#' that is not a valid value", pID, def._defaultValue );
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
                        bOk = validateGlobalVariableTarget( def, pGlobalVariableManager ) && bOk;
                        break;
                    }
                    case UserSettingTargetKind::Handler:
                    {
                        bOk = validateHandlerTarget( def, registry ) && bOk;
                        break;
                    }
                }
                return validateConditions( schema, def ) && bOk;
            }

            /** @brief 품질 번들 하나(묶인 설정이 열거형인지, custom 값, 선택지마다 프리셋, 프리셋 값, 자동 감지 규칙)를 검사합니다. */
            [[nodiscard]] static bool validateScalabilityGroup( const UserSettingsSchema& schema, const ScalabilityGroupDef& group )
            {
                const UserSettingDef* pGroupSetting = schema.findSetting( group._settingID );
                if ( pGroupSetting == nullptr || pGroupSetting->_type != UserSettingType::Enum )
                {
                    SW_LOG_ERROR( "<Scalability> setting '%#' is not an enum setting", group._settingID.c_str() );
                    return false;
                }
                bool   bOk = true;
                string normalized;
                if ( UserSettingsSchema::normalizeValue( *pGroupSetting, group._customValue, normalized ) != UserSettingValueResult::Accepted )
                {
                    SW_LOG_ERROR( "<Scalability> '%#': custom value '%#' is not an option", group._settingID.c_str(), group._customValue );
                    bOk = false;
                }
                // 선택지 하나하나(custom 제외)가 프리셋 하나다. 프리셋이 없는 선택지를 고르면 묶인 설정이 아무것도 바뀌지 않는다.
                for ( const UserSettingOption& option : pGroupSetting->_listOption )
                {
                    if ( StringUtil::equals( option._value, group._customValue ) )
                        continue;
                    bool bHasPreset = false;
                    for ( const ScalabilityPresetDef& preset : group._listPreset )
                    {
                        bHasPreset = bHasPreset || StringUtil::equals( preset._name.c_str(), option._value );
                    }
                    if ( bHasPreset == false )
                    {
                        SW_LOG_ERROR( "<Scalability> '%#': option '%#' has no <Preset>", group._settingID.c_str(), option._value );
                        bOk = false;
                    }
                }
                for ( const ScalabilityPresetDef& preset : group._listPreset )
                {
                    if ( UserSettingsSchema::normalizeValue( *pGroupSetting, preset._name.c_str(), normalized ) != UserSettingValueResult::Accepted )
                    {
                        SW_LOG_ERROR( "<Scalability> '%#': preset '%#' is not an option", group._settingID.c_str(), preset._name.c_str() );
                        bOk = false;
                    }
                    for ( const ScalabilityPresetValue& value : preset._listValue )
                    {
                        const UserSettingDef* pMember = schema.findSetting( value._settingID );
                        if ( pMember == nullptr || pMember == pGroupSetting )
                        {
                            SW_LOG_ERROR( "<Scalability> preset '%#' sets an unknown setting '%#'", preset._name.c_str(), value._settingID.c_str() );
                            bOk = false;
                            continue;
                        }
                        if ( UserSettingsSchema::normalizeValue( *pMember, value._value, normalized ) != UserSettingValueResult::Accepted )
                        {
                            SW_LOG_ERROR( "<Scalability> preset '%#' sets '%#' to an invalid value '%#'", preset._name.c_str(), value._settingID.c_str(), value._value );
                            bOk = false;
                        }
                    }
                }
                for ( const ScalabilityAutoDetectRule& rule : group._listAutoDetectRule )
                {
                    if ( UserSettingsSchema::normalizeValue( *pGroupSetting, rule._preset.c_str(), normalized ) != UserSettingValueResult::Accepted )
                    {
                        SW_LOG_ERROR( "<AutoDetect> names an unknown preset '%#'", rule._preset.c_str() );
                        bOk = false;
                    }
                }
                if ( group._autoDetectFallback.empty() == false &&
                     UserSettingsSchema::normalizeValue( *pGroupSetting, group._autoDetectFallback.c_str(), normalized ) != UserSettingValueResult::Accepted )
                {
                    SW_LOG_ERROR( "<Scalability> '%#': autoDetectFallback '%#' is not an option", group._settingID.c_str(), group._autoDetectFallback.c_str() );
                    bOk = false;
                }
                return bOk;
            }
        };

        static_assert( UserSettingsSchemaInternal::isTypeTraitsInEnumOrder(), "kArrTypeTraits rows must follow UserSettingType order" );
        static_assert( std::size( UserSettingsSchemaInternal::kArrTypeTraits ) == static_cast<size_t>( UserSettingType::String ) + 1,
                       "kArrTypeTraits needs one row per UserSettingType" );
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
        XMLDocument doc;
        string      absPath;
        if ( doc.loadResource( resourcePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to read the user settings schema '%#'", resourcePath );
            return false;
        }
        return loadRoot( doc.getRoot( "UserSettingsSchema" ), resourcePath );
    }

    bool UserSettingsSchema::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "Failed to parse the user settings schema '%#'", sourceName );
            return false;
        }
        return loadRoot( doc.getRoot( "UserSettingsSchema" ), sourceName );
    }

    bool UserSettingsSchema::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Internal = UserSettingsSchemaInternal;
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the root element is not <UserSettingsSchema>", sourceName );
            return false;
        }

        // 다 읽은 뒤에만 합친다. 반쯤 읽은 덧붙이기가 남으면 다음 검사가 엉뚱한 줄을 탓하기 때문이다.
        UserSettingsSchema overlay;
        bool               bOk = XMLNameCheck::reportUnknownAttributes( root, Internal::kArrRootAttribute, sourceName );
        int32              version{ 0 }; // 적지 않으면 판 0 이다
        if ( root.readOptionalAttributeInRange( "version", 0, Internal::kMaxSchemaVersion, version ) == false )
            bOk = false;
        overlay._version = static_cast<uint32>( version );

        const uint32 orderBase = static_cast<uint32>( _listSetting.size() );
        for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, "Category", true ) )
            {
                UserSettingCategoryDef category;
                bOk = Internal::readCategory( child, static_cast<uint32>( _listCategory.size() + overlay._listCategory.size() ), sourceName, category ) && bOk;
                if ( category._id.empty() == false )
                    overlay._listCategory.push_back( category );
            }
            else if ( StringUtil::equals( pName, "Setting", true ) )
            {
                UserSettingDef def;
                bOk = Internal::readSetting( child, orderBase + static_cast<uint32>( overlay._listSetting.size() ), sourceName, def ) && bOk;
                if ( def._id.empty() == false )
                    overlay._listSetting.push_back( def );
            }
            else if ( StringUtil::equals( pName, "Scalability", true ) )
            {
                ScalabilityGroupDef group;
                bOk = Internal::readScalability( child, sourceName, group ) && bOk;
                overlay._listScalabilityGroup.push_back( group );
            }
            else if ( StringUtil::equals( pName, "Upgrade", true ) )
            {
                UserSettingsUpgradeStep step;
                bOk = Internal::readUpgrade( child, sourceName, step ) && bOk;
                overlay._listUpgradeStep.push_back( step );
            }
            else
            {
                SW_LOG_ERROR( "%#: unknown element <%#>", sourceName, pName );
                bOk = false;
            }
        }

        // 겹치는 id 는 덧붙이기에서도 오류다. 게임마다 다른 값은 기본값 덮어쓰기(게임 프리셋)로 준다.
        for ( const UserSettingDef& def : overlay._listSetting )
        {
            bool bDuplicate = findSetting( def._id ) != nullptr;
            for ( const UserSettingDef& other : overlay._listSetting )
            {
                bDuplicate = bDuplicate || ( &other != &def && other._id == def._id );
            }
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

        // 버전 번호는 가장 큰 것을 쓴다. 게임 덧붙이기가 자기 버전 단계를 더하면 그 버전까지 올라간다.
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
        {
            _mapSettingIndex[_listSetting[settingIndex]._id] = settingIndex;
        }
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
        return VectorUtil::findBy<&UserSettingCategoryDef::_id>( _listCategory, id );
    }

    const ScalabilityGroupDef* UserSettingsSchema::findScalabilityGroupOf( const hashed_string& settingID ) const
    {
        for ( const ScalabilityGroupDef& group : _listScalabilityGroup )
        {
            if ( group._settingID == settingID )
                return &group;
            for ( const ScalabilityPresetDef& preset : group._listPreset )
            {
                for ( const ScalabilityPresetValue& value : preset._listValue )
                {
                    if ( value._settingID == settingID )
                        return &group;
                }
            }
        }
        return nullptr;
    }

    const UserSettingTypeTraits& UserSettingsSchema::getTypeTraits( UserSettingType type )
    {
        return UserSettingsSchemaInternal::kArrTypeTraits[static_cast<size_t>( type )];
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
                if ( def._listOption.empty() || def._optionProvider.empty() == false )
                {
                    // 선택지를 공급자에서 받는 열거형은 여기서 형식만 본다(빈 텍스트는 "자동"). 공급자와 고정 선택지를 함께 적은 설정의 고정 선택지는
                    // 공급자가 비었을 때의 대체 목록이라, 공급자가 주는 값(모니터 해상도)을 막지 않는다.
                    outValue = string( value );
                    return UserSettingValueResult::Accepted;
                }
                for ( const UserSettingOption& option : def._listOption )
                {
                    if ( StringUtil::equals( value, option._value ) )
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

    bool UserSettingsSchema::validate( const UserSettingsHandlerRegistry& registry, GlobalVariableManager* pGlobalVariableManager ) const
    {
        using Internal = UserSettingsSchemaInternal;
        bool bOk       = true;
        for ( const UserSettingDef& def : _listSetting )
        {
            bOk = Internal::validateSetting( *this, def, registry, pGlobalVariableManager ) && bOk;
        }
        for ( const ScalabilityGroupDef& group : _listScalabilityGroup )
        {
            bOk = Internal::validateScalabilityGroup( *this, group ) && bOk;
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
