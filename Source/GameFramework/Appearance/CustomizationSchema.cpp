#include "pch.h"

#include "GameFramework/Appearance/CustomizationSchema.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Appearance/AppearanceTypes.h"
#include "GameFramework/Appearance/AppearanceXmlUtil.h"

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
                    if ( StringUtil::equals( pName, toString( kind ), true ) )
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
                    if ( StringUtil::equals( text, string_view( toString( kind ) ), true ) )
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

            static void reportUnknownParamAttributes( const XmlNode& node, CustomizationKind kind, AppearanceLoadReport& report, string_view sourceName )
            {
                switch ( kind )
                {
                    case CustomizationKind::Slider:
                    {
                        (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrSliderAttribute, report, sourceName );
                        break;
                    }
                    case CustomizationKind::Color:
                    {
                        (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrColorAttribute, report, sourceName );
                        break;
                    }
                    case CustomizationKind::Choice:
                    {
                        (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrChoiceAttribute, report, sourceName );
                        break;
                    }
                    case CustomizationKind::Attachment:
                    {
                        (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrAttachmentAttribute, report, sourceName );
                        break;
                    }
                }
            }

            static void readDrive( const XmlNode& node, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName, const hashed_string& schemaId )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrDriveAttribute, report, sourceName );
                CustomizationDriveDef drive;
                drive._target  = AppearanceXmlUtil::readName( node, "target" );
                drive._from    = node.getAttributeFloat( "from", inoutParam._min );
                drive._to      = node.getAttributeFloat( "to", inoutParam._max );
                drive._channel = node.getAttributeInt( "channel", 0 );
                if ( parseDriveKind( node.getAttributeText( "kind" ), drive._kind ) == false )
                {
                    report.addError( "%#: schema '%#' parameter '%#' has unknown drive kind '%#'", sourceName, schemaId.c_str(), inoutParam._name.c_str(), node.getAttributeText( "kind" ) );
                    return;
                }
                const bool bSliderDrive = isSliderDrive( drive._kind );
                const bool bFits        = ( inoutParam._kind == CustomizationKind::Slider && bSliderDrive ) || ( inoutParam._kind == CustomizationKind::Color && bSliderDrive == false );
                if ( bFits == false || drive._target.empty() )
                {
                    report.addError( "%#: schema '%#' parameter '%#' cannot drive %# '%#'", sourceName, schemaId.c_str(), inoutParam._name.c_str(), toString( drive._kind ), drive._target.c_str() );
                    return;
                }
                if ( drive._kind == CustomizationDriveKind::DyeChannel && ( drive._channel < 0 || drive._channel > kMaxDyeChannel ) )
                {
                    report.addError( "%#: schema '%#' parameter '%#' dye channel %# is outside 0..3", sourceName, schemaId.c_str(), inoutParam._name.c_str(), drive._channel );
                    return;
                }
                inoutParam._listDrive.push_back( drive );
            }

            static void readOption( const XmlNode& node, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName, const hashed_string& schemaId )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrOptionAttribute, report, sourceName );
                CustomizationOptionDef option;
                option._name               = AppearanceXmlUtil::readName( node, "name" );
                option._visual             = AppearanceXmlUtil::readName( node, "visual" );
                option._variant            = AppearanceXmlUtil::readName( node, "variant" );
                option._materialVariant    = AppearanceXmlUtil::readName( node, "materialVariant" );
                const hashed_string prefab = AppearanceXmlUtil::readName( node, "prefab" );
                const hashed_string sprite = AppearanceXmlUtil::readName( node, "sprite" );
                option._asset              = prefab.empty() ? sprite : prefab;
                option._offset             = AppearanceXmlUtil::readFloat3( node, "offset", float3::Zero );
                option._rotation           = AppearanceXmlUtil::readFloat3( node, "rotation", float3::Zero );
                const bool bChoiceOnly     = option._visual.empty() == false || option._variant.empty() == false || option._materialVariant.empty() == false;
                if ( option._name.empty() || inoutParam.findOption( option._name ) != nullptr )
                    report.addError( "%#: schema '%#' parameter '%#' has an option without a name ('None' reads as no name) or a duplicate option '%#'", sourceName, schemaId.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( inoutParam._kind == CustomizationKind::Attachment && bChoiceOnly )
                    report.addError( "%#: schema '%#' attachment '%#' option '%#' may only name a prefab or sprite", sourceName, schemaId.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( inoutParam._kind == CustomizationKind::Choice && option._asset.empty() == false )
                    report.addError( "%#: schema '%#' choice '%#' option '%#' cannot name a prefab or sprite", sourceName, schemaId.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( prefab.empty() == false && sprite.empty() == false )
                    report.addError( "%#: schema '%#' option '%#' names both a prefab and a sprite", sourceName, schemaId.c_str(), option._name.c_str() );
                else
                    inoutParam._listOption.push_back( option );
            }

            static void readCondition( const XmlNode& node, const CustomizationSchemaDef& schema, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrConditionAttribute, report, sourceName );
                CustomizationConditionDef condition;
                condition._parameter = AppearanceXmlUtil::readName( node, "parameter" );
                AppearanceXmlUtil::readNameList( node, "options", condition._listOption );
                // 조건은 앞에 선언한 매개변수만 — 아직 목록에 들어가지 않은 자기 자신도 못 가리킨다(순환이 생길 수 없다).
                const CustomizationParamDef* pTarget = schema.findParameter( condition._parameter );
                if ( pTarget == nullptr )
                {
                    report.addError( "%#: schema '%#' parameter '%#' condition must name a parameter declared before it, not '%#'", sourceName, schema._id.c_str(), inoutParam._name.c_str(),
                                     condition._parameter.c_str() );
                    return;
                }
                if ( pTarget->_kind == CustomizationKind::Slider )
                {
                    condition._min = node.getAttributeFloat( "min", pTarget->_min );
                    condition._max = node.getAttributeFloat( "max", pTarget->_max );
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

            static void readParameter( const XmlNode& node, CustomizationKind kind, CustomizationSchemaDef& inoutSchema, AppearanceLoadReport& report, string_view sourceName )
            {
                reportUnknownParamAttributes( node, kind, report, sourceName );
                CustomizationParamDef param;
                param._kind     = kind;
                param._name     = AppearanceXmlUtil::readName( node, "name" );
                param._category = AppearanceXmlUtil::readName( node, "category" );
                param._symmetry = AppearanceXmlUtil::readName( node, "symmetry" );
                param._socket   = AppearanceXmlUtil::readName( node, "socket" );
                param._min      = node.getAttributeFloat( "min", param._min );
                param._max      = node.getAttributeFloat( "max", param._max );
                if ( param._name.empty() || inoutSchema.findParameter( param._name ) != nullptr )
                {
                    report.addError( "%#: schema '%#' has a parameter without a name or a duplicate '%#'", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                    return;
                }
                if ( kind == CustomizationKind::Slider && ( param._max > param._min ) == false )
                    report.addError( "%#: schema '%#' slider '%#' needs min < max", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                if ( kind == CustomizationKind::Attachment && param._socket.empty() )
                    report.addError( "%#: schema '%#' attachment '%#' needs a socket", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                if ( kind == CustomizationKind::Slider )
                    param._default = MathUtil::clamp( node.getAttributeFloat( "default", param._min ), param._min, param._max );
                else
                    param._default = param._min;
                if ( kind == CustomizationKind::Color )
                    param._defaultColor = AppearanceXmlUtil::readFloat4( node, "default", param._defaultColor );
                for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
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
                        AppearanceXmlUtil::reportUnknownChild( node, child, report, sourceName );
                }
                if ( kind == CustomizationKind::Choice || kind == CustomizationKind::Attachment )
                {
                    param._defaultOption = AppearanceXmlUtil::readName( node, "default" );
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
    const utf8* toString( CustomizationKind kind )
    {
        switch ( kind )
        {
            case CustomizationKind::Slider:
                return "Slider";
            case CustomizationKind::Color:
                return "Color";
            case CustomizationKind::Choice:
                return "Choice";
            case CustomizationKind::Attachment:
                return "Attachment";
        }
        return "Unknown";
    }

    const utf8* toString( CustomizationDriveKind kind )
    {
        switch ( kind )
        {
            case CustomizationDriveKind::Morph:
                return "Morph";
            case CustomizationDriveKind::BoneProportion:
                return "BoneProportion";
            case CustomizationDriveKind::MaterialScalar:
                return "MaterialScalar";
            case CustomizationDriveKind::MaterialColor:
                return "MaterialColor";
            case CustomizationDriveKind::DyeChannel:
                return "DyeChannel";
            case CustomizationDriveKind::PaletteSwap:
                return "PaletteSwap";
        }
        return "Unknown";
    }

    const CustomizationOptionDef* CustomizationParamDef::findOption( const hashed_string& option ) const
    {
        for ( const CustomizationOptionDef& entry : _listOption )
        {
            if ( entry._name == option )
                return &entry;
        }
        return nullptr;
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
        for ( const CustomizationParamDef& param : _listParameter )
        {
            if ( param._name == name )
                return &param;
        }
        return nullptr;
    }

    bool CustomizationSchemaCatalog::loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXmlUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XmlNode schemaNode = root.findChild(); schemaNode; schemaNode = schemaNode.findNextSibling() )
        {
            if ( StringUtil::equals( schemaNode.getName(), "Schema", true ) == false )
            {
                AppearanceXmlUtil::reportUnknownChild( root, schemaNode, report, sourceName );
                continue;
            }
            (void)AppearanceXmlUtil::reportUnknownAttributes( schemaNode, CustomizationSchemaInternal::kArrSchemaAttribute, report, sourceName );
            CustomizationSchemaDef schema;
            schema._id = AppearanceXmlUtil::readName( schemaNode, "id" );
            if ( schema._id.empty() || findSchema( schema._id ) != nullptr )
            {
                report.addError( "%#: <Schema> without an id or with a duplicate id '%#'", sourceName, schema._id.c_str() );
                continue;
            }
            for ( XmlNode paramNode = schemaNode.findChild(); paramNode; paramNode = paramNode.findNextSibling() )
            {
                CustomizationKind kind = CustomizationKind::Slider;
                if ( CustomizationSchemaInternal::parseKind( paramNode.getName(), kind ) )
                    CustomizationSchemaInternal::readParameter( paramNode, kind, schema, report, sourceName );
                else
                    AppearanceXmlUtil::reportUnknownChild( schemaNode, paramNode, report, sourceName );
            }
            // 공유 코드 · 네트워크는 매개변수 이름을 32 비트 해시로 싣는다 — 한 스키마 안에서 겹치면 안 된다.
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
        for ( const CustomizationSchemaDef& schema : _listSchema )
        {
            if ( schema._id == id )
                return &schema;
        }
        return nullptr;
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
            // 기본값도 같은 격자로 — 정규화를 두 번 해도 같아야 한다(전송한 값과 기본값이 비트까지 같다).
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
            // 조건 대상이 꺼져 있으면 그 값도 없는 것으로 친다(사슬 — 앞 매개변수부터 판정되므로 한 번에 끝난다).
            if ( isActive( *pTarget, schema, normalizedValues ) == false )
                return false;
            if ( pTarget->_kind == CustomizationKind::Slider )
            {
                const bool bInRange = condition._min <= pValue->_number._x && pValue->_number._x <= condition._max;
                if ( bInRange == false )
                    return false;
            }
            else if ( AppearanceXmlUtil::containsName( condition._listOption, pValue->_option ) == false )
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
            if ( partner._symmetry != pParam->_symmetry || partner._name == pParam->_name || partner._kind != pParam->_kind )
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
