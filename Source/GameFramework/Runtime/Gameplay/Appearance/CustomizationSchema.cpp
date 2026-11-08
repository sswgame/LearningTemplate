#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct CustomizationSchemaInternal
        {
            // 매개변수 종류마다 받는 칸이다(그 밖의 칸은 오류 — 칸 구조체 하나를 함께 쓰므로 종류마다 막는다).
            static constexpr const utf8* kArrSliderField[]     = { "name", "category", "symmetry", "min", "max", "default" };
            static constexpr const utf8* kArrColorField[]      = { "name", "category", "symmetry", "default", "palette" };
            static constexpr const utf8* kArrChoiceField[]     = { "name", "category", "symmetry", "default" };
            static constexpr const utf8* kArrSwatchAttribute[] = { "name", "color" };
            static constexpr const utf8* kArrAttachmentField[] = { "name", "category", "symmetry", "default", "socket" };

            static bool isSliderDrive( CustomizationDriveKind kind )
            {
                return kind == CustomizationDriveKind::Morph || kind == CustomizationDriveKind::BoneProportion || kind == CustomizationDriveKind::MaterialScalar;
            }

            static void reportUnknownParamAttributes( const XMLNode& node, CustomizationKind kind, AppearanceLoadReport& report, string_view sourceName )
            {
                switch ( kind )
                {
                    case CustomizationKind::Slider:
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrSliderField, report, sourceName );
                        return;
                    case CustomizationKind::Color:
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrColorField, report, sourceName );
                        return;
                    case CustomizationKind::Choice:
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrChoiceField, report, sourceName );
                        return;
                    case CustomizationKind::Attachment:
                        (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrAttachmentField, report, sourceName );
                        return;
                }
            }

            static void readDrive( const XMLNode& node, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName, const hashed_string& schemaID )
            {
                // 구간을 적지 않으면 매개변수의 범위다(이 데이터의 값). 깨진 값 · 모르는 종류 · 채널 0..3 밖은 오류다(PROPERTY 가 본다).
                CustomizationDriveDef drive;
                drive._from = inoutParam._min;
                drive._to   = inoutParam._max;
                if ( AppearanceXMLUtil::readFields( node, drive, report, sourceName, { "include", "exclude" } ) == false )
                    return;
                AppearanceXMLUtil::readNameList( node, "include", drive._listInclude );
                AppearanceXMLUtil::readNameList( node, "exclude", drive._listExclude );
                if ( drive.isSectionTargeted() && drive.isMaterialDrive() == false )
                {
                    report.addErrorWithID( "Appearance.DriveSectionTargetInvalid", "%#: schema '%#' parameter '%#' drive %# cannot name sections (include/exclude is for material drives)", sourceName, schemaID.c_str(),
                                           inoutParam._name.c_str(), getEnumName( drive._kind ) );
                    return;
                }
                const bool bSliderDrive = isSliderDrive( drive._kind );
                const bool bFits        = ( inoutParam._kind == CustomizationKind::Slider && bSliderDrive ) || ( inoutParam._kind == CustomizationKind::Color && bSliderDrive == false );
                if ( bFits == false || drive._target.empty() )
                {
                    report.addErrorWithID( "Appearance.ParameterCannotDrive", "%#: schema '%#' parameter '%#' cannot drive %# '%#'", sourceName, schemaID.c_str(), inoutParam._name.c_str(), getEnumName( drive._kind ), drive._target.c_str() );
                    return;
                }
                inoutParam._listDrive.push_back( drive );
            }

            static void readOption( const XMLNode& node, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName, const hashed_string& schemaID )
            {
                CustomizationOptionParams params;
                (void)AppearanceXMLUtil::readFields( node, params, report, sourceName ); // 오류는 보고서에 쌓인다
                CustomizationOptionDef option;
                option._name               = params._name;
                option._visual             = params._visual;
                option._variant            = params._variant;
                option._materialVariant    = params._materialVariant;
                const hashed_string prefab = params._prefab;
                const hashed_string sprite = params._sprite;
                option._asset              = prefab.empty() ? sprite : prefab;
                option._offset             = params._offset;
                option._rotation           = params._rotation;
                const bool bChoiceOnly     = option._visual.empty() == false || option._variant.empty() == false || option._materialVariant.empty() == false;
                if ( option._name.empty() || inoutParam.findOption( option._name ) != nullptr )
                    report.addErrorWithID( "Appearance.OptionNameInvalid", "%#: schema '%#' parameter '%#' has an option without a name ('None' reads as no name) or a duplicate option '%#'", sourceName, schemaID.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( inoutParam._kind == CustomizationKind::Attachment && bChoiceOnly )
                    report.addErrorWithID( "Appearance.OptionAssetKindInvalid", "%#: schema '%#' attachment '%#' option '%#' may only name a prefab or sprite", sourceName, schemaID.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( inoutParam._kind == CustomizationKind::Choice && option._asset.empty() == false )
                    report.addErrorWithID( "Appearance.OptionAssetKindInvalid", "%#: schema '%#' choice '%#' option '%#' cannot name a prefab or sprite", sourceName, schemaID.c_str(), inoutParam._name.c_str(), option._name.c_str() );
                else if ( prefab.empty() == false && sprite.empty() == false )
                    report.addErrorWithID( "Appearance.OptionAssetKindInvalid", "%#: schema '%#' option '%#' names both a prefab and a sprite", sourceName, schemaID.c_str(), option._name.c_str() );
                else
                    inoutParam._listOption.push_back( option );
            }

            static void readCondition( const XMLNode& node, const CustomizationSchemaDef& schema, CustomizationParamDef& inoutParam, AppearanceLoadReport& report, string_view sourceName )
            {
                CustomizationConditionParams params;
                CustomizationConditionDef    condition;
                (void)AppearanceXMLUtil::readFields( node, params, report, sourceName, { "options" } ); // 구간은 대상을 안 뒤에 다시 읽는다
                condition._parameter = params._parameter;
                AppearanceXMLUtil::readNameList( node, "options", condition._listOption );
                // 조건은 앞에 선언한 매개변수만 가리킬 수 있다. 아직 목록에 들어가지 않은 자기 자신도 가리킬 수 없으므로 순환이 생길 수 없다.
                const CustomizationParamDef* pTarget = schema.findParameter( condition._parameter );
                if ( pTarget == nullptr )
                {
                    report.addErrorWithID( "Appearance.ConditionNotDeclaredBefore", "%#: schema '%#' parameter '%#' condition must name a parameter declared before it, not '%#'", sourceName, schema._id.c_str(), inoutParam._name.c_str(),
                                           condition._parameter.c_str() );
                    return;
                }
                if ( pTarget->_kind == CustomizationKind::Slider )
                {
                    // 적지 않은 구간 끝은 대상 슬라이더의 범위다.
                    condition._min = node.findAttribute( "min" ) != nullptr ? params._min : pTarget->_min;
                    condition._max = node.findAttribute( "max" ) != nullptr ? params._max : pTarget->_max;
                }
                else if ( pTarget->_kind == CustomizationKind::Color || condition._listOption.empty() )
                {
                    report.addErrorWithID( "Appearance.ConditionShape", "%#: schema '%#' parameter '%#' condition on '%#' needs options (choice) or min/max (slider)", sourceName, schema._id.c_str(), inoutParam._name.c_str(),
                                           condition._parameter.c_str() );
                    return;
                }
                for ( const hashed_string& option : condition._listOption )
                {
                    if ( pTarget->findOption( option ) == nullptr )
                        report.addErrorWithID( "Appearance.OptionUnknown", "%#: schema '%#' parameter '%#' condition names unknown option '%#' of '%#'", sourceName, schema._id.c_str(), inoutParam._name.c_str(), option.c_str(),
                                               condition._parameter.c_str() );
                }
                inoutParam._listCondition.push_back( condition );
            }

            static void readParameter( const XMLNode& node, CustomizationKind kind, CustomizationSchemaDef& inoutSchema, AppearanceLoadReport& report, string_view sourceName )
            {
                reportUnknownParamAttributes( node, kind, report, sourceName ); // 종류마다 받는 칸
                CustomizationParamEntryParams params;
                (void)AppearanceXMLUtil::readFields( node, params, report, sourceName, { "default" } ); // 기본값은 종류마다 꼴이 달라 아래에서 읽는다
                CustomizationParamDef param;
                param._kind     = kind;
                param._name     = params._name;
                param._category = params._category;
                param._symmetry = params._symmetry;
                param._socket   = params._socket;  // 부착 매개변수만 필수 — 아래에서 확인한다
                param._palette  = params._palette; // 견본은 카탈로그가 스키마를 다 읽은 뒤 채운다(`loadFromXML`)
                param._min      = params._min;
                param._max      = params._max;
                if ( param._name.empty() || inoutSchema.findParameter( param._name ) != nullptr )
                {
                    report.addErrorWithID( "Appearance.ParameterNameInvalid", "%#: schema '%#' has a parameter without a name or a duplicate '%#'", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                    return;
                }
                if ( kind == CustomizationKind::Slider && ( param._max > param._min ) == false )
                    report.addErrorWithID( "Appearance.SliderRange", "%#: schema '%#' slider '%#' needs min < max", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                if ( kind == CustomizationKind::Attachment && param._socket.empty() )
                    report.addErrorWithID( "Appearance.AttachmentNeedsSocket", "%#: schema '%#' attachment '%#' needs a socket", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
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
                    if ( StringUtil::equals( pName, "Drive" ) && bHasOption == false )
                        readDrive( child, param, report, sourceName, inoutSchema._id );
                    else if ( StringUtil::equals( pName, "Option" ) && bHasOption )
                        readOption( child, param, report, sourceName, inoutSchema._id );
                    else if ( StringUtil::equals( pName, "Condition" ) )
                        readCondition( child, inoutSchema, param, report, sourceName );
                    else
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                }
                if ( kind == CustomizationKind::Choice || kind == CustomizationKind::Attachment )
                {
                    param._defaultOption = XMLReadUtil::readOptionalName( node, "default" ); // 적지 않으면 첫 선택지
                    if ( param._listOption.empty() )
                        report.addErrorWithID( "Appearance.ParameterNoOption", "%#: schema '%#' parameter '%#' has no <Option>", sourceName, inoutSchema._id.c_str(), param._name.c_str() );
                    else if ( param._defaultOption.empty() )
                        param._defaultOption = param._listOption.front()._name;
                    else if ( param.findOption( param._defaultOption ) == nullptr )
                        report.addErrorWithID( "Appearance.DefaultNotAnOption", "%#: schema '%#' parameter '%#' default '%#' is not one of its options", sourceName, inoutSchema._id.c_str(), param._name.c_str(),
                                               param._defaultOption.c_str() );
                }
                inoutSchema._listParameter.push_back( param );
            }

            /** @brief 견본 묶음(`<Palette id><Swatch name color/></Palette>`)을 읽습니다. 견본이 없거나 이름이 겹치면 오류입니다. */
            static void readPalette( const XMLNode& node, vector<CustomizationSchemaCatalog::Palette>& inoutListPalette, AppearanceLoadReport& report, string_view sourceName )
            {
                CustomizationSchemaCatalog::Palette palette;
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrIDAttribute, report, sourceName );
                palette._id = AppearanceXMLUtil::readRequiredName( node, "id", report, sourceName );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Swatch" ) == false )
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                        continue;
                    }
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrSwatchAttribute, report, sourceName );
                    const hashed_string name = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    float4              color{};
                    AppearanceXMLUtil::readRequiredFloat4( child, "color", color, report, sourceName );
                    if ( VectorUtil::contains( palette._listSwatchName, name ) )
                    {
                        report.addErrorWithID( "Appearance.IDDuplicate", "%#: palette '%#' repeats swatch '%#'", sourceName, palette._id.c_str(), name.c_str() );
                        continue;
                    }
                    palette._listSwatchName.push_back( name );
                    palette._listSwatch.push_back( color );
                }
                if ( palette._id.empty() )
                    return;
                const bool bRepeated = VectorUtil::containsBy<&CustomizationSchemaCatalog::Palette::_id>( inoutListPalette, palette._id );
                if ( bRepeated || palette._listSwatch.empty() )
                {
                    report.addErrorWithID( "Appearance.PaletteInvalid", "%#: palette '%#' needs a unique id and at least one <Swatch>", sourceName, palette._id.c_str() );
                    return;
                }
                inoutListPalette.push_back( std::move( palette ) );
            }

            static constexpr const utf8* kArrIDAttribute[] = { "id" };
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

    bool CustomizationSchemaCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode schemaNode = root.findChild(); schemaNode; schemaNode = schemaNode.findNextSibling() )
        {
            if ( StringUtil::equals( schemaNode.getName(), "Palette" ) )
            {
                CustomizationSchemaInternal::readPalette( schemaNode, _listPalette, report, sourceName );
                continue;
            }
            if ( StringUtil::equals( schemaNode.getName(), "Schema" ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, schemaNode, report, sourceName );
                continue;
            }

            CustomizationSchemaDef schema;
            (void)AppearanceXMLUtil::readFields( schemaNode, schema, report, sourceName );
            if ( schema._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findSchema( schema._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <Schema> has a duplicate id '%#'", sourceName, schema._id.c_str() );
                continue;
            }
            for ( XMLNode paramNode = schemaNode.findChild(); paramNode; paramNode = paramNode.findNextSibling() )
            {
                CustomizationKind kind = CustomizationKind::Slider;
                if ( game::getSubsystem<TypeRegistry>().enumFromString( paramNode.getName(), kind ) ) // 원소 이름이 매개변수 종류다
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
                        report.addErrorWithID( "Appearance.IDHashClash", "%#: schema '%#' parameters '%#' and '%#' share a 32-bit name hash - rename one", sourceName, schema._id.c_str(),
                                               schema._listParameter[lhs]._name.c_str(), schema._listParameter[rhs]._name.c_str() );
                }
            }
            _listSchema.push_back( schema );
        }
        // 견본 묶음은 파일 어디에 적어도 된다 — 스키마를 다 읽은 뒤 이름으로 잇는다. 모르는 묶음은 오류다(자유 색으로 조용히 풀지 않는다).
        for ( CustomizationSchemaDef& schema : _listSchema )
        {
            for ( CustomizationParamDef& param : schema._listParameter )
            {
                if ( param._palette.empty() )
                    continue;
                const Palette* pPalette = findPalette( param._palette );
                if ( pPalette == nullptr )
                {
                    report.addErrorWithID( "Appearance.PaletteUnknown", "%#: schema '%#' color '%#' names unknown palette '%#'", sourceName, schema._id.c_str(), param._name.c_str(), param._palette.c_str() );
                    continue;
                }
                param._listSwatch = pPalette->_listSwatch;
            }
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const CustomizationSchemaCatalog::Palette* CustomizationSchemaCatalog::findPalette( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&Palette::_id>( _listPalette, id );
    }

    float4 CustomizationParamDef::snapToSwatch( const float4& color ) const
    {
        if ( _listSwatch.empty() )
            return color;
        const float4* pBest    = &_listSwatch.front();
        float32       bestDist = MathUtil::kMaxFloat;
        for ( const float4& swatch : _listSwatch )
        {
            const float4  delta = swatch - color;
            const float32 dist  = delta._x * delta._x + delta._y * delta._y + delta._z * delta._z + delta._w * delta._w;
            if ( dist < bestDist )
            {
                bestDist = dist;
                pBest    = &swatch;
            }
        }
        return *pBest;
    }

    const CustomizationSchemaDef* CustomizationSchemaCatalog::findSchema( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&CustomizationSchemaDef::_id>( _listSchema, id );
    }

    uint32 CustomizationParamDef::quantizeSlider( float32 value ) const
    {
        const float32 range = _max - _min;
        if ( range <= 0.0f )
            return 0;
        const float32 unit = MathUtil::saturate( ( value - _min ) / range );
        return static_cast<uint32>( unit * static_cast<float32>( kSliderSteps ) + 0.5f );
    }

    float32 CustomizationParamDef::dequantizeSlider( uint32 step ) const
    {
        const uint32 clamped = MathUtil::min( step, kSliderSteps );
        return _min + ( _max - _min ) * ( static_cast<float32>( clamped ) / static_cast<float32>( kSliderSteps ) );
    }

    uint32 CustomizationParamDef::quantizeColorChannel( float32 value )
    {
        return static_cast<uint32>( MathUtil::saturate( value ) * static_cast<float32>( kColorSteps ) + 0.5f );
    }

    float32 CustomizationParamDef::dequantizeColorChannel( uint32 step )
    {
        return static_cast<float32>( MathUtil::min( step, kColorSteps ) ) / static_cast<float32>( kColorSteps );
    }

    void CustomizationSchemaDef::normalize( const CustomizationValueSet& values, CustomizationValueSet& outValues, vector<hashed_string>* pOutListDropped ) const
    {
        outValues.clear();
        for ( const CustomizationParamDef& param : _listParameter )
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
                value._number = float4( param.dequantizeSlider( param.quantizeSlider( value._number._x ) ), 0.0f, 0.0f, 0.0f );
            }
            else if ( param._kind == CustomizationKind::Color )
            {
                value._number = param.snapToSwatch( value._number ); // 견본 묶음이 있으면 가장 가까운 견본(공유 코드로 온 임의 색도 팔레트로 돌아온다)
                value._number = float4( CustomizationParamDef::dequantizeColorChannel( CustomizationParamDef::quantizeColorChannel( value._number._x ) ), CustomizationParamDef::dequantizeColorChannel( CustomizationParamDef::quantizeColorChannel( value._number._y ) ),
                                        CustomizationParamDef::dequantizeColorChannel( CustomizationParamDef::quantizeColorChannel( value._number._z ) ), CustomizationParamDef::dequantizeColorChannel( CustomizationParamDef::quantizeColorChannel( value._number._w ) ) );
            }
            outValues.setValue( value );
        }
        if ( pOutListDropped == nullptr )
            return;
        for ( const CustomizationValue& given : values.getValues() )
        {
            if ( findParameter( given._parameter ) == nullptr )
                pOutListDropped->push_back( given._parameter );
        }
    }

    bool CustomizationParamDef::isActive( const CustomizationSchemaDef& schema, const CustomizationValueSet& normalizedValues ) const
    {
        for ( const CustomizationConditionDef& condition : _listCondition )
        {
            const CustomizationParamDef* pTarget = schema.findParameter( condition._parameter );
            const CustomizationValue*    pValue  = normalizedValues.findValue( condition._parameter );
            if ( pTarget == nullptr || pValue == nullptr )
                return false;
            // 조건 대상이 꺼져 있으면 그 값도 없는 것으로 본다. 앞 매개변수부터 차례로 판정하므로 연쇄 조건도 한 번에 끝난다.
            if ( pTarget->isActive( schema, normalizedValues ) == false )
                return false;
            if ( pTarget->_kind == CustomizationKind::Slider )
            {
                const bool bInRange = condition._min <= pValue->_number._x && pValue->_number._x <= condition._max;
                if ( bInRange == false )
                    return false;
            }
            else if ( VectorUtil::contains( condition._listOption, pValue->_option ) == false )
            {
                return false;
            }
        }
        return true;
    }

    bool CustomizationSchemaDef::applyValue( const CustomizationValue& value, bool bSymmetric, CustomizationValueSet& inoutValues ) const
    {
        const CustomizationParamDef* pParam = findParameter( value._parameter );
        if ( pParam == nullptr )
            return false;
        inoutValues.setValue( value );
        if ( bSymmetric == false || pParam->_symmetry.empty() )
            return true;
        for ( const CustomizationParamDef& partner : _listParameter )
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

    float32 CustomizationParamDef::computeDriveValue( const CustomizationDriveDef& drive, float32 value ) const
    {
        const float32 range = _max - _min;
        const float32 unit  = range > 0.0f ? MathUtil::saturate( ( value - _min ) / range ) : 0.0f;
        return drive._from + ( drive._to - drive._from ) * unit;
    }
} // namespace sw
