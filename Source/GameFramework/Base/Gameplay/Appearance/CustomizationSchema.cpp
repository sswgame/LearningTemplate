#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/CustomizationSchema.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct CustomizationSchemaInternal
        {
            static constexpr const utf8* kArrSchemaAttribute[]     = { "id" };
            static constexpr const utf8* kArrSliderAttribute[]     = { "name", "category", "symmetry", "min", "max", "default" };
            static constexpr const utf8* kArrColorAttribute[]      = { "name", "category", "symmetry", "default" };
            static constexpr const utf8* kArrChoiceAttribute[]     = { "name", "category", "symmetry", "default" };
            static constexpr const utf8* kArrAttachmentAttribute[] = { "name", "category", "symmetry", "default", "socket" };
            static constexpr const utf8* kArrDriveAttribute[]      = { "kind", "target", "from", "to", "channel" };
            static constexpr const utf8* kArrOptionAttribute[]     = { "name", "visual", "variant", "materialVariant", "prefab", "sprite", "offset", "rotation" };
            static constexpr const utf8* kArrConditionAttribute[]  = { "parameter", "options", "min", "max" };
            static constexpr int32       kMaxDyeChannel            = 3;

            [[nodiscard]] static bool parseKind( const utf8* pName, CustomizationKind& outKind )
            {
                constexpr CustomizationKind kArrKind[] = { CustomizationKind::Slider, CustomizationKind::Color, CustomizationKind::Choice, CustomizationKind::Attachment };
                for ( const CustomizationKind kind : kArrKind )
                {
                    if ( StringUtil::equals( pName, getEnumName( kind ), true ) )
                    {
                        outKind = kind;
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] static bool parseDriveKind( string_view text, CustomizationDriveKind& outKind )
            {
                constexpr CustomizationDriveKind kArrKind[] = { CustomizationDriveKind::Morph, CustomizationDriveKind::BoneProportion, CustomizationDriveKind::MaterialScalar,
                                                                CustomizationDriveKind::MaterialColor, CustomizationDriveKind::DyeChannel, CustomizationDriveKind::PaletteSwap };
                for ( const CustomizationDriveKind kind : kArrKind )
                {
                    if ( StringUtil::equals( text, string_view( getEnumName( kind ) ), true ) )
                    {
                        outKind = kind;
                        return true;
                    }
                }
                return false;
            }

            static bool isSliderDrive( CustomizationDriveKind kind )
            {
                return kind == CustomizationDriveKind::Morph || kind == CustomizationDriveKind::BoneProportion || kind == CustomizationDriveKind::MaterialScalar;
            }

            static void reportUnknownParamAttributes( const XMLNode& node, CustomizationKind kind, AppearanceLoadReport& report, string_view sourceName )
            {
                switch ( kind )
                {
                    case CustomizationKind::Slider:
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrSliderAttribute, report, sourceName );
                        break;
                    }
                    case CustomizationKind::Color:
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrColorAttribute, report, sourceName );
                        break;
                    }
                    case CustomizationKind::Choice:
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrChoiceAttribute, report, sourceName );
                        break;
                    }
                    case CustomizationKind::Attachment:
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrAttachmentAttribute, report, sourceName );
                        break;
                    }
                }
            }

            static void readDrive( const XMLNode& node, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName, const hashed_string& schemaID )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrDriveAttribute, report, sourceName );
                CustomizationDriveDef drive;
                drive._target = AppearanceXMLUtil::readRequiredName( node, "target", report, sourceName );
                // 구간을 적지 않으면 매개변수의 범위다(이 데이터의 값). 깨진 값은 오류이고 칸은 그대로다.
                drive._from = inoutParam._min;
                drive._to   = inoutParam._max;
                AppearanceXMLUtil::readOptionalFloat( node, "from", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, drive._from, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "to", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, drive._to, report, sourceName );
                AppearanceXMLUtil::readOptionalInt( node, "channel", MathUtil::kMinInt32, MathUtil::kMaxInt32, drive._channel, report, sourceName );
                if ( parseDriveKind( node.getAttributeText( "kind" ), drive._kind ) == false )
                {
                    report.addError( "%#: schema '%#' parameter '%#' has unknown drive kind '%#'", sourceName, schemaID.c_str(), inoutParam._name.c_str(), node.getAttributeText( "kind" ) );
                    return;
                }
                const bool bSliderDrive = isSliderDrive( drive._kind );
                const bool bFits        = ( inoutParam._kind == CustomizationKind::Slider && bSliderDrive ) || ( inoutParam._kind == CustomizationKind::Color && bSliderDrive == false );
                if ( bFits == false || drive._target.empty() )
                {
                    report.addError( "%#: schema '%#' parameter '%#' cannot drive %# '%#'", sourceName, schemaID.c_str(), inoutParam._name.c_str(), getEnumName( drive._kind ), drive._target.c_str() );
                    return;
                }
                if ( drive._kind == CustomizationDriveKind::DyeChannel && ( drive._channel < 0 || drive._channel > kMaxDyeChannel ) )
                {
                    report.addError( "%#: schema '%#' parameter '%#' dye channel %# is outside 0..3", sourceName, schemaID.c_str(), inoutParam._name.c_str(), drive._channel );
                    return;
                }
                inoutParam._listDrive.push_back( drive );
            }

            static void readOption( const XMLNode& node, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName, const hashed_string& schemaID )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrOptionAttribute, report, sourceName );
                CustomizationOptionDef option;
                option._name               = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                option._visual             = AppearanceXMLUtil::readOptionalName( node, "visual" );
                option._variant            = AppearanceXMLUtil::readOptionalName( node, "variant" );
                option._materialVariant    = AppearanceXMLUtil::readOptionalName( node, "materialVariant" );
                const hashed_string prefab = AppearanceXMLUtil::readOptionalName( node, "prefab" );
                const hashed_string sprite = AppearanceXMLUtil::readOptionalName( node, "sprite" );
                option._asset              = prefab.empty() ? sprite : prefab;
                AppearanceXMLUtil::readOptionalFloat3( node, "offset", option._offset, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat3( node, "rotation", option._rotation, report, sourceName );
                const bool bChoiceOnly = option._visual.empty() == false || option._variant.empty() == false || option._materialVariant.empty() == false;
                if ( option._name.empty() || inoutParam.findOption( option._name ) != nullptr )
                    report.addError( "%#: schema '%#' parameter '%#' has an option without a name ('None' reads as no name) or a duplicate option '%#'", sourceName, schemaID.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( inoutParam._kind == CustomizationKind::Attachment && bChoiceOnly )
                    report.addError( "%#: schema '%#' attachment '%#' option '%#' may only name a prefab or sprite", sourceName, schemaID.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( inoutParam._kind == CustomizationKind::Choice && option._asset.empty() == false )
                    report.addError( "%#: schema '%#' choice '%#' option '%#' cannot name a prefab or sprite", sourceName, schemaID.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( prefab.empty() == false && sprite.empty() == false )
                    report.addError( "%#: schema '%#' option '%#' names both a prefab and a sprite", sourceName, schemaID.c_str(), option._name.c_str() );
                else
                    inoutParam._listOption.push_back( option );
            }

            static void readCondition( const XMLNode& node, const CustomizationSchemaDef& schema, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrConditionAttribute, report, sourceName );
                CustomizationConditionDef condition;
                condition._parameter = AppearanceXMLUtil::readRequiredName( node, "parameter", report, sourceName );
                AppearanceXMLUtil::readNameList( node, "options", condition._listOption );
                // 조건은 앞에 선언한 매개변수만 가리킬 수 있다. 아직 목록에 들어가지 않은 자기 자신도 가리킬 수 없으므로 순환이 생길 수 없다.
                const CustomizationParamDef* pTarget = schema.findParameter( condition._parameter );
                if ( pTarget == nullptr )
                {
                    report.addError( "%#: schema '%#' parameter '%#' condition must name a parameter declared before it, not '%#'", sourceName, schema._id.c_str(), inoutParam._name.c_str(),
                                     condition._parameter.c_str() );
                    return;
                }
                if ( pTarget->_kind == CustomizationKind::Slider )
                {
                    condition._min = pTarget->_min;
                    condition._max = pTarget->_max;
                    AppearanceXMLUtil::readOptionalFloat( node, "min", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, condition._min, report, sourceName );
                    AppearanceXMLUtil::readOptionalFloat( node, "max", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, condition._max, report, sourceName );
                }
                else if ( pTarget->_kind == CustomizationKind::Color || condition._listOption.empty() )
                {
                    report.addError( "%#: schema '%#' parameter '%#' condition on '%#' needs options (choice) or min/max (slider)", sourceName, schema._id.c_str(), inoutParam._name.c_str(),
                                     condition._parameter.c_str() );
                    return;
                }
                for ( const hashed_string& option : condition._listOption )
                {
                    if ( pTarget->findOption( option ) == nullptr )
                        report.addError( "%#: schema '%#' parameter '%#' condition names unknown option '%#' of '%#'", sourceName, schema._id.c_str(), inoutParam._name.c_str(), option.c_str(),
                                         condition._parameter.c_str() );
                }
                inoutParam._listCondition.push_back( condition );
            }

            static void readParameter( const XMLNode& node, CustomizationKind kind, CustomizationSchemaDef& inoutSchema, AppearanceLoadReport& report, string_view sourceName )
            {
                reportUnknownParamAttributes( node, kind, report, sourceName );
                CustomizationParamDef param;
                param._kind     = kind;
                param._name     = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                param._category = AppearanceXMLUtil::readOptionalName( node, "category" );
                param._symmetry = AppearanceXMLUtil::readOptionalName( node, "symmetry" );
                param._socket   = AppearanceXMLUtil::readOptionalName( node, "socket" ); // 부착 매개변수만 필수 — 아래에서 확인한다
                AppearanceXMLUtil::readOptionalFloat( node, "min", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, param._min, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "max", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, param._max, report, sourceName );
                if ( param._name.empty() || inoutSchema.findParameter( param._name ) != nullptr )
                {
                    report.addError( "%#: schema '%#' has a parameter without a name or a duplicate '%#'", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                    return;
                }
                if ( kind == CustomizationKind::Slider && ( param._max > param._min ) == false )
                    report.addError( "%#: schema '%#' slider '%#' needs min < max", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                if ( kind == CustomizationKind::Attachment && param._socket.empty() )
                    report.addError( "%#: schema '%#' attachment '%#' needs a socket", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                // 슬라이더 기본값을 적지 않으면 범위의 아래 끝이다. 범위 밖 기본값은 자르지 않고 오류다.
                param._default = param._min;
                if ( kind == CustomizationKind::Slider )
                    AppearanceXMLUtil::readOptionalFloat( node, "default", param._min, param._max, param._default, report, sourceName );
                if ( kind == CustomizationKind::Color )
                    AppearanceXMLUtil::readOptionalFloat4( node, "default", param._defaultColor, report, sourceName );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName      = child.getName();
                    const bool  bHasOption = kind == CustomizationKind::Choice || kind == CustomizationKind::Attachment;
                    if ( StringUtil::equals( pName, "Drive", true ) && bHasOption == false )
                        readDrive( child, param, report, sourceName, inoutSchema._id );
                    else if ( StringUtil::equals( pName, "Option", true ) && bHasOption )
                        readOption( child, param, report, sourceName, inoutSchema._id );
                    else if ( StringUtil::equals( pName, "Condition", true ) )
                        readCondition( child, inoutSchema, param, report, sourceName );
                    else
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                }
                if ( kind == CustomizationKind::Choice || kind == CustomizationKind::Attachment )
                {
                    param._defaultOption = AppearanceXMLUtil::readOptionalName( node, "default" ); // 적지 않으면 첫 선택지
                    if ( param._listOption.empty() )
                        report.addError( "%#: schema '%#' parameter '%#' has no <Option>", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                    else if ( param._defaultOption.empty() )
                        param._defaultOption = param._listOption.front()._name;
                    else if ( param.findOption( param._defaultOption ) == nullptr )
                        report.addError( "%#: schema '%#' parameter '%#' default '%#' is not one of its options", sourceName, inoutSchema._id.c_str(), param._name.c_str(),
                                         param._defaultOption.c_str() );
                }
                inoutSchema._listParameter.push_back( param );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const CustomizationOptionDef* CustomizationParamDef::findOption( const hashed_string& option ) const
    {
        return VectorUtil::findBy<&CustomizationOptionDef::_name>( _listOption, option );
    }

    CustomizationValue CustomizationParamDef::makeDefaultValue() const
    {
        CustomizationValue value;
        value._parameter = _name;
        switch ( _kind )
        {
            case CustomizationKind::Slider:
            {
                value._number = float4( _default, 0.0f, 0.0f, 0.0f );
                break;
            }
            case CustomizationKind::Color:
            {
                value._number = _defaultColor;
                break;
            }
            case CustomizationKind::Choice:
            case CustomizationKind::Attachment:
            {
                value._option = _defaultOption;
                break;
            }
        }
        return value;
    }

    const CustomizationParamDef* CustomizationSchemaDef::findParameter( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&CustomizationParamDef::_name>( _listParameter, name );
    }

    bool CustomizationSchemaCatalog::loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode schemaNode = root.findChild(); schemaNode; schemaNode = schemaNode.findNextSibling() )
        {
            if ( StringUtil::equals( schemaNode.getName(), "Schema", true ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, schemaNode, report, sourceName );
                continue;
            }
            (void)AppearanceXMLUtil::reportUnknownAttributes( schemaNode, CustomizationSchemaInternal::kArrSchemaAttribute, report, sourceName );
            CustomizationSchemaDef schema;
            schema._id = AppearanceXMLUtil::readRequiredName( schemaNode, "id", report, sourceName );
            if ( schema._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findSchema( schema._id ) != nullptr )
            {
                report.addError( "%#: <Schema> has a duplicate id '%#'", sourceName, schema._id.c_str() );
                continue;
            }
            for ( XMLNode paramNode = schemaNode.findChild(); paramNode; paramNode = paramNode.findNextSibling() )
            {
                CustomizationKind kind = CustomizationKind::Slider;
                if ( CustomizationSchemaInternal::parseKind( paramNode.getName(), kind ) )
                    CustomizationSchemaInternal::readParameter( paramNode, kind, schema, report, sourceName );
                else
                    AppearanceXMLUtil::reportUnknownChild( schemaNode, paramNode, report, sourceName );
            }
            // 공유 코드와 네트워크는 매개변수 이름을 32 비트 해시로 싣는다. 한 스키마 안에서 해시가 겹치면 안 된다.
            for ( size_t lhs = 0; lhs < schema._listParameter.size(); ++lhs )
            {
                for ( size_t rhs = lhs + 1; rhs < schema._listParameter.size(); ++rhs )
                {
                    if ( static_cast<uint32>( schema._listParameter[lhs]._name.getHash() ) == static_cast<uint32>( schema._listParameter[rhs]._name.getHash() ) )
                        report.addError( "%#: schema '%#' parameters '%#' and '%#' share a 32-bit name hash - rename one", sourceName, schema._id.c_str(),
                                         schema._listParameter[lhs]._name.c_str(), schema._listParameter[rhs]._name.c_str() );
                }
            }
            _listSchema.push_back( schema );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const CustomizationSchemaDef* CustomizationSchemaCatalog::findSchema( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&CustomizationSchemaDef::_id>( _listSchema, id );
    }

    uint32 CustomizationUtil::quantizeSlider( const CustomizationParamDef& param, float32 value )
    {
        const float32 range = param._max - param._min;
        if ( range <= 0.0f )
            return 0;
        const float32 unit = MathUtil::saturate( ( value - param._min ) / range );
        return static_cast<uint32>( unit * static_cast<float32>( kSliderSteps ) + 0.5f );
    }

    float32 CustomizationUtil::dequantizeSlider( const CustomizationParamDef& param, uint32 step )
    {
        const uint32 clamped = MathUtil::min( step, kSliderSteps );
        return param._min + ( param._max - param._min ) * ( static_cast<float32>( clamped ) / static_cast<float32>( kSliderSteps ) );
    }

    uint32 CustomizationUtil::quantizeColorChannel( float32 value )
    {
        return static_cast<uint32>( MathUtil::saturate( value ) * static_cast<float32>( kColorSteps ) + 0.5f );
    }

    float32 CustomizationUtil::dequantizeColorChannel( uint32 step )
    {
        return static_cast<float32>( MathUtil::min( step, kColorSteps ) ) / static_cast<float32>( kColorSteps );
    }

    void CustomizationUtil::normalize( const CustomizationSchemaDef& schema, const CustomizationValueSet& values, CustomizationValueSet& outValues, vector<hashed_string>* pOutListDropped )
    {
        outValues.clear();
        for ( const CustomizationParamDef& param : schema._listParameter )
        {
            CustomizationValue        value       = param.makeDefaultValue();
            const CustomizationValue* pGiven      = values.findValue( param._name );
            const bool                bOptionKind = param._kind == CustomizationKind::Choice || param._kind == CustomizationKind::Attachment;
            if ( pGiven != nullptr && bOptionKind == false )
                value._number = pGiven->_number;
            else if ( pGiven != nullptr && param.findOption( pGiven->_option ) != nullptr )
                value._option = pGiven->_option;
            else if ( pGiven != nullptr && pOutListDropped != nullptr )
                pOutListDropped->push_back( pGiven->_option );
            // 기본값도 같은 격자로 맞춘다. 정규화를 두 번 해도 결과가 같아야 하고, 전송한 값과 기본값이 비트까지 같아야 하기 때문이다.
            if ( param._kind == CustomizationKind::Slider )
            {
                value._number = float4( dequantizeSlider( param, quantizeSlider( param, value._number._x ) ), 0.0f, 0.0f, 0.0f );
            }
            else if ( param._kind == CustomizationKind::Color )
            {
                value._number = float4( dequantizeColorChannel( quantizeColorChannel( value._number._x ) ), dequantizeColorChannel( quantizeColorChannel( value._number._y ) ),
                                        dequantizeColorChannel( quantizeColorChannel( value._number._z ) ), dequantizeColorChannel( quantizeColorChannel( value._number._w ) ) );
            }
            outValues.setValue( value );
        }
        if ( pOutListDropped == nullptr )
            return;
        for ( const CustomizationValue& given : values.getValues() )
        {
            if ( schema.findParameter( given._parameter ) == nullptr )
                pOutListDropped->push_back( given._parameter );
        }
    }

    bool CustomizationUtil::isActive( const CustomizationParamDef& param, const CustomizationSchemaDef& schema, const CustomizationValueSet& normalizedValues )
    {
        for ( const CustomizationConditionDef& condition : param._listCondition )
        {
            const CustomizationParamDef* pTarget = schema.findParameter( condition._parameter );
            const CustomizationValue*    pValue  = normalizedValues.findValue( condition._parameter );
            if ( pTarget == nullptr || pValue == nullptr )
                return false;
            // 조건 대상이 꺼져 있으면 그 값도 없는 것으로 본다. 앞 매개변수부터 차례로 판정하므로 연쇄 조건도 한 번에 끝난다.
            if ( isActive( *pTarget, schema, normalizedValues ) == false )
                return false;
            if ( pTarget->_kind == CustomizationKind::Slider )
            {
                const bool bInRange = condition._min <= pValue->_number._x && pValue->_number._x <= condition._max;
                if ( bInRange == false )
                    return false;
            }
            else if ( AppearanceXMLUtil::containsName( condition._listOption, pValue->_option ) == false )
            {
                return false;
            }
        }
        return true;
    }

    bool CustomizationUtil::applyValue( const CustomizationSchemaDef& schema, const CustomizationValue& value, bool bSymmetric, CustomizationValueSet& inoutValues )
    {
        const CustomizationParamDef* pParam = schema.findParameter( value._parameter );
        if ( pParam == nullptr )
            return false;
        inoutValues.setValue( value );
        if ( bSymmetric == false || pParam->_symmetry.empty() )
            return true;
        for ( const CustomizationParamDef& partner : schema._listParameter )
        {
            const bool bNotMirrorPartner = partner._symmetry != pParam->_symmetry || partner._name == pParam->_name || partner._kind != pParam->_kind;
            if ( bNotMirrorPartner )
                continue;
            CustomizationValue mirrored = value;
            mirrored._parameter         = partner._name;
            inoutValues.setValue( mirrored );
        }
        return true;
    }

    float32 CustomizationUtil::computeDriveValue( const CustomizationParamDef& param, const CustomizationDriveDef& drive, float32 value )
    {
        const float32 range = param._max - param._min;
        const float32 unit  = range > 0.0f ? MathUtil::saturate( ( value - param._min ) / range ) : 0.0f;
        return drive._from + ( drive._to - drive._from ) * unit;
    }
} // namespace sw
