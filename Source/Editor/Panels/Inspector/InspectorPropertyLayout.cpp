#include "pch.h"

// 인스펙터는 에디터 기능이라 Shipping 에는 없다 — 에디터 메타데이터(카테고리 · 표시 이름 · 숨김)도 Shipping 빌드에서 빠진다.

#if !defined( SW_SHIPPING )

    #include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

    #include "Core/Math/MathUtil.h"
    #include "Core/String/StringUtil.h"

    #include "Editor/Common/Widgets/EditorListFilter.h"

    #include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    void InspectorPropertyLayout::collectTypeChain( const TypeInfo& type, vector<const TypeInfo*>& outListType )
    {
        outListType.clear();
        for ( const TypeInfo* pType = &type; pType != nullptr; pType = pType->getParentType() )
        {
            for ( const TypeInfo* pSeen : outListType )
            {
                if ( pSeen == pType )
                {
                    std::reverse( outListType.begin(), outListType.end() );
                    return;
                }
            }
            outListType.push_back( pType );
        }
        std::reverse( outListType.begin(), outListType.end() );
    }

    void InspectorPropertyLayout::collectPropertyGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                         vector<InspectorPropertyGroup>& outListGroup )
    {
        outListGroup.clear();
        // 상속분은 기반 → 파생 순으로 온다(파생이 같은 이름을 다시 선언하면 그 자리에서 바뀐다 — `TypeInfo::getPropertiesWithBase`).
        type.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( prop._metadata._bHideInInspector == SW_TRUE )
                return;
            for ( const hashed_string& drawnName : listDrawnName )
            {
                if ( drawnName == prop._name )
                    return;
            }
            if ( filter.matchesAny( { string_view{ prop._name.c_str() }, string_view{ getPropertyLabel( prop ) },
                                      string_view{ prop._metadata._category.c_str() } } ) == false )
                return;

            const string_view       category = prop._metadata._category.empty() ? string_view{ "General" } : string_view{ prop._metadata._category.c_str() };
            InspectorPropertyGroup* pGroup   = nullptr;
            for ( InspectorPropertyGroup& group : outListGroup )
            {
                if ( group._category == category )
                {
                    pGroup = &group;
                    break;
                }
            }
            if ( pGroup == nullptr )
            {
                outListGroup.push_back( InspectorPropertyGroup{ string{ category }, {} } );
                pGroup = &outListGroup.back();
            }
            pGroup->_listProperty.push_back( &prop );
        }, true );
    }

    const utf8* InspectorPropertyLayout::getPropertyLabel( const PropertyInfo& prop )
    {
        if ( prop._metadata._displayName.empty() == false )
            return prop._metadata._displayName.c_str();
        if ( prop._listAlias.empty() == false && prop._listAlias.front().empty() == false )
            return prop._listAlias.front().c_str();
        return prop._name.c_str();
    }

    InspectorDisplayUnit InspectorPropertyLayout::getDisplayUnit( const PropertyInfo& prop )
    {
        InspectorDisplayUnit unit{};
        const string*        pUnits = prop.findCustomMeta( hashed_string( "Units" ) );
        if ( pUnits == nullptr || pUnits->empty() )
            return unit;
        if ( *pUnits == "rad" )
        {
            unit._scale     = MathUtil::RadianToDegree;
            unit._dragSpeed = kAngleDragSpeed;
            unit._suffix    = "deg";
            return unit;
        }
        if ( *pUnits == "ratio" )
        {
            unit._scale     = 100.0f;
            unit._dragSpeed = kPercentDragSpeed;
            unit._suffix    = "%";
            return unit;
        }
        // 0..100 으로 저장한 백분율은 그대로 보이고 글자만 `%` 다.
        if ( *pUnits == "percent" )
        {
            unit._dragSpeed = kPercentDragSpeed;
            unit._suffix    = "%";
            return unit;
        }
        unit._suffix = *pUnits;
        return unit;
    }

    string InspectorPropertyLayout::appendUnitSuffix( const utf8* pNumberFormat, const string& suffix )
    {
        string format = ( pNumberFormat != nullptr ) ? pNumberFormat : "";
        if ( suffix.empty() )
            return format;
        format += " ";
        for ( const utf8 letter : suffix )
        {
            format += letter;
            if ( letter == '%' )
                format += '%';
        }
        return format;
    }

    InspectorNumericRange InspectorPropertyLayout::getNumericRange( const PropertyInfo& prop )
    {
        const PropertyMetadata& meta = prop._metadata;
        InspectorNumericRange   range;
        range._bHasClampMin = meta._bHasMinRange != SW_FALSE;
        range._bHasClampMax = meta._bHasMaxRange != SW_FALSE;
        range._clampMin     = static_cast<float64>( meta._minRange );
        range._clampMax     = static_cast<float64>( meta._maxRange );

        range._bHasWidgetMin = meta._bHasUiMinRange != SW_FALSE || range._bHasClampMin;
        range._bHasWidgetMax = meta._bHasUiMaxRange != SW_FALSE || range._bHasClampMax;
        range._widgetMin     = meta._bHasUiMinRange != SW_FALSE ? static_cast<float64>( meta._uiMinRange ) : range._clampMin;
        range._widgetMax     = meta._bHasUiMaxRange != SW_FALSE ? static_cast<float64>( meta._uiMaxRange ) : range._clampMax;

        const bool bSliderMeta = prop.findCustomMeta( hashed_string( "Slider" ) ) != nullptr;
        range._bSlider         = meta.hasFullUiRange() || ( meta.hasFullRange() && bSliderMeta );
        return range;
    }

    float64 InspectorPropertyLayout::clampToAllowedRange( const InspectorNumericRange& range, float64 value )
    {
        if ( range._bHasClampMin && value < range._clampMin )
            value = range._clampMin;
        if ( range._bHasClampMax && range._clampMax < value )
            value = range._clampMax;
        return value;
    }

    bool InspectorPropertyLayout::isColorRequested( const PropertyInfo& prop )
    {
        if ( prop._metadata._bColorHdr != SW_FALSE || prop.findCustomMeta( hashed_string( "Color" ) ) != nullptr )
            return true;
        return StringUtil::stristr( prop._name.c_str(), "color" ) != nullptr;
    }

    bool InspectorPropertyLayout::matchesFileFilter( string_view filter, string_view path )
    {
        filter = StringUtil::trim( filter );
        if ( filter.empty() )
            return true;
        size_t start = 0;
        while ( start <= filter.size() )
        {
            const size_t      separator = filter.find_first_of( ";,", start );
            const size_t      end       = ( separator == string_view::npos ) ? filter.size() : separator;
            const string_view pattern   = StringUtil::trim( filter.substr( start, end - start ) );
            // `*.png` 는 확장자 비교, `*` 는 무엇이든, 그 밖은 파일 이름 끝 비교다.
            if ( pattern == "*" || pattern == "*.*" )
                return true;
            const string_view suffix = StringUtil::startsWith( pattern, "*" ) ? pattern.substr( 1 ) : pattern;
            if ( suffix.empty() == false && StringUtil::endsWith( path, suffix, true ) )
                return true;
            if ( separator == string_view::npos )
                break;
            start = separator + 1;
        }
        return false;
    }

    string InspectorPropertyLayout::formatParameterList( const vector<FunctionParameterInfo>& listParameter )
    {
        string text;
        for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
        {
            const FunctionParameterInfo& parameter = listParameter[paramIndex];
            if ( paramIndex > 0 )
                text += ", ";
            text += parameter._typeName;
            if ( parameter._name.empty() == false )
            {
                text += " ";
                text += parameter._name;
            }
            if ( parameter.hasDefaultValue() )
            {
                text += " = ";
                text += parameter._defaultValue;
            }
        }
        return text;
    }
} // namespace sw::editor

#endif // !SW_SHIPPING
