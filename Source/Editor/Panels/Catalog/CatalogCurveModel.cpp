#include "pch.h"

#include "Editor/Panels/Catalog/CatalogCurveModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Panels/Catalog/CatalogDocument.h"

namespace sw::editor
{
    namespace
    {
        struct CatalogCurveModelInternal
        {
            static constexpr float32 kNewKeyTimeStep   = 0.1f;
            static constexpr float32 kNewKeyFirstValue = 1.0f;

            static int32 findChildNamed( const CatalogXMLText& parsed, const CatalogXMLElement& element, string_view name )
            {
                for ( const int32 childIndex : element._listChild )
                {
                    if ( string_view{ parsed.getElement( childIndex )._name } == name )
                        return childIndex;
                }
                return -1;
            }

            static bool isGradientChannel( string_view name )
            {
                return name == "_red" || name == "_green" || name == "_blue" || name == "_alpha";
            }

            /** @brief 속성 값을 실수로 읽습니다. 없거나 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool readFloat( const CatalogXMLElement& element, string_view name, float32& outValue )
            {
                const int32 attributeIndex = element.findAttribute( name );
                if ( attributeIndex < 0 )
                    return false;
                return StringUtil::parseFloat( element._listAttribute[static_cast<size_t>( attributeIndex )]._value, outValue );
            }

            static string makeSegment( const CatalogXMLElement& element )
            {
                string      segment   = element._name;
                const int32 nameIndex = element.findAttribute( "_name" );
                if ( nameIndex >= 0 )
                {
                    segment += "(";
                    segment += element._listAttribute[static_cast<size_t>( nameIndex )]._value;
                    segment += ")";
                }
                return segment;
            }

            static void readKeys( const CatalogXMLText& parsed, const CatalogXMLElement& listKey, vector<CatalogCurveKey>& outListKey )
            {
                for ( const int32 keyIndex : listKey._listChild )
                {
                    const CatalogXMLElement& keyElement = parsed.getElement( keyIndex );
                    if ( string_view{ keyElement._name } != CatalogCurveModel::kKeyElementName )
                        continue;
                    CatalogCurveKey key;
                    key._elementIndex              = keyIndex;
                    const bool bTime               = readFloat( keyElement, "_time", key._time );
                    const bool bValue              = readFloat( keyElement, "_value", key._value );
                    key._bValid                    = bTime && bValue;
                    const int32 interpolationIndex = keyElement.findAttribute( "_interpolation" );
                    if ( interpolationIndex >= 0 )
                        key._interpolation = keyElement._listAttribute[static_cast<size_t>( interpolationIndex )]._value;
                    outListKey.push_back( std::move( key ) );
                }
            }

            static void walk( const CatalogXMLText& parsed, int32 elementIndex, const string& prefix, vector<CatalogCurve>& outListCurve )
            {
                const CatalogXMLElement& element        = parsed.getElement( elementIndex );
                bool                     bHasCurveChild = false;
                for ( const int32 childIndex : element._listChild )
                {
                    bHasCurveChild = bHasCurveChild || findChildNamed( parsed, parsed.getElement( childIndex ), CatalogCurveModel::kKeyListName ) >= 0;
                }
                for ( const int32 childIndex : element._listChild )
                {
                    const CatalogXMLElement& child = parsed.getElement( childIndex );
                    if ( string_view{ child._name } == CatalogCurveModel::kKeyListName )
                        continue;
                    const string label        = prefix.empty() ? makeSegment( child ) : prefix + "/" + makeSegment( child );
                    const int32  listKeyIndex = findChildNamed( parsed, child, CatalogCurveModel::kKeyListName );
                    // 키가 없는 채널(`<_red/>`)은 모양만으로는 곡선인지 알 수 없다 - 형제에 곡선이 있는 색 채널 이름일 때만 빈 곡선으로 본다.
                    const bool bEmptyChannel = child._listChild.empty() && bHasCurveChild && isGradientChannel( child._name );
                    if ( listKeyIndex >= 0 || bEmptyChannel )
                    {
                        CatalogCurve curve;
                        curve._label        = label;
                        curve._channel      = child._name;
                        curve._elementIndex = childIndex;
                        curve._parentIndex  = elementIndex;
                        if ( listKeyIndex >= 0 )
                            readKeys( parsed, parsed.getElement( listKeyIndex ), curve._listKey );
                        outListCurve.push_back( std::move( curve ) );
                        continue;
                    }
                    walk( parsed, childIndex, label, outListCurve );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void CatalogCurveModel::collectCurves( const CatalogXMLText& parsed, int32 ownerElement, vector<CatalogCurve>& outListCurve )
    {
        outListCurve.clear();
        const bool bInvalid = parsed.isParsed() == false || ownerElement < 0 || static_cast<size_t>( ownerElement ) >= parsed.getElements().size();
        if ( bInvalid )
            return;
        CatalogCurveModelInternal::walk( parsed, ownerElement, string{}, outListCurve );
    }

    void CatalogCurveModel::collectGradients( const vector<CatalogCurve>& listCurve, vector<CatalogGradient>& outListGradient )
    {
        outListGradient.clear();
        for ( size_t index = 0; index < listCurve.size(); ++index )
        {
            if ( listCurve[index]._channel != "_red" )
                continue;
            CatalogGradient gradient;
            gradient._red = static_cast<int32>( index );
            for ( size_t other = 0; other < listCurve.size(); ++other )
            {
                if ( listCurve[other]._parentIndex != listCurve[index]._parentIndex )
                    continue;
                if ( listCurve[other]._channel == "_green" )
                    gradient._green = static_cast<int32>( other );
                else if ( listCurve[other]._channel == "_blue" )
                    gradient._blue = static_cast<int32>( other );
                else if ( listCurve[other]._channel == "_alpha" )
                    gradient._alpha = static_cast<int32>( other );
            }
            const bool bComplete = gradient._green >= 0 && gradient._blue >= 0 && gradient._alpha >= 0;
            if ( bComplete == false )
                continue;
            // 라벨은 빨강 곡선의 라벨에서 마지막 채널 구간을 뺀 것이다.
            const string& redLabel = listCurve[index]._label;
            const size_t  slash    = redLabel.rfind( '/' );
            gradient._label        = slash == string::npos ? redLabel : redLabel.substr( 0, slash );
            outListGradient.push_back( std::move( gradient ) );
        }
    }

    FloatCurve CatalogCurveModel::makeFloatCurve( const CatalogCurve& curve )
    {
        FloatCurve result;
        for ( const CatalogCurveKey& key : curve._listKey )
        {
            if ( key._bValid == false )
                continue;
            FloatCurveKey converted;
            converted._time  = key._time;
            converted._value = key._value;
            if ( key._interpolation == "Constant" )
                converted._interpolation = CurveInterpolation::Constant;
            else if ( key._interpolation == "Linear" )
                converted._interpolation = CurveInterpolation::Linear;
            result._listKey.push_back( converted );
        }
        result.sortAndComputeAutoTangents();
        return result;
    }

    float32 CatalogCurveModel::computeEndTime( const CatalogCurve& curve )
    {
        float32 endTime = 1.0f;
        for ( const CatalogCurveKey& key : curve._listKey )
        {
            if ( key._bValid && key._time > endTime )
                endTime = key._time;
        }
        return endTime;
    }

    bool CatalogCurveModel::sample( const CatalogCurve& curve, float32 endTime, uint32 sampleCount, vector<float32>& outListValue )
    {
        outListValue.clear();
        if ( sampleCount < 2 )
            return false;
        const FloatCurve floatCurve = makeFloatCurve( curve );
        if ( floatCurve._listKey.empty() )
            return false;
        for ( uint32 index = 0; index < sampleCount; ++index )
        {
            const float32                time  = endTime * static_cast<float32>( index ) / static_cast<float32>( sampleCount - 1 );
            const std::optional<float32> value = floatCurve.evaluate( time );
            outListValue.push_back( value.value_or( 0.0f ) ); // 키가 있으면 값이 있다(위에서 거른다)
        }
        return true;
    }

    bool CatalogCurveModel::addKey( CatalogDocument& inoutDocument, int32 curveElementIndex )
    {
        using Internal                             = CatalogCurveModelInternal;
        const CatalogXMLText& parsed               = inoutDocument.getParsed();
        const bool            bInvalidElementIndex = parsed.isParsed() == false || curveElementIndex < 0 || static_cast<size_t>( curveElementIndex ) >= parsed.getElements().size();
        if ( bInvalidElementIndex )
            return false;
        int32 listKeyIndex = Internal::findChildNamed( parsed, parsed.getElement( curveElementIndex ), kKeyListName );
        if ( listKeyIndex < 0 )
        {
            if ( inoutDocument.addChild( curveElementIndex, kKeyListName, {} ) == false )
                return false;
            // 고치면 문서가 다시 훑여 인덱스가 바뀐다 - 곡선 원소는 더한 원소보다 앞이라 같은 인덱스로 남는다.
            listKeyIndex = Internal::findChildNamed( inoutDocument.getParsed(), inoutDocument.getParsed().getElement( curveElementIndex ), kKeyListName );
            if ( listKeyIndex < 0 )
                return false;
        }

        // 새 키의 시각과 값: 마지막 키 뒤 한 걸음에 같은 값, 키가 없으면 시각 0 값 1.
        float32                  time    = 0.0f;
        float32                  value   = Internal::kNewKeyFirstValue;
        const CatalogXMLElement& listKey = inoutDocument.getParsed().getElement( listKeyIndex );
        for ( const int32 keyIndex : listKey._listChild )
        {
            const CatalogXMLElement& keyElement = inoutDocument.getParsed().getElement( keyIndex );
            float32                  keyTime    = 0.0f;
            float32                  keyValue   = 0.0f;
            const bool               bRead      = Internal::readFloat( keyElement, "_time", keyTime ) && Internal::readFloat( keyElement, "_value", keyValue );
            if ( bRead && keyTime + Internal::kNewKeyTimeStep > time )
            {
                time  = keyTime + Internal::kNewKeyTimeStep;
                value = keyValue;
            }
        }
        StringBuilder<constant::kMaxBuffer32> timeText;
        StringBuilder<constant::kMaxBuffer32> valueText;
        timeText.append( time );
        valueText.append( value );
        vector<CatalogXMLNewAttribute> listAttribute;
        listAttribute.push_back( CatalogXMLNewAttribute{ "_time", string{ timeText.view() } } );
        listAttribute.push_back( CatalogXMLNewAttribute{ "_value", string{ valueText.view() } } );
        listAttribute.push_back( CatalogXMLNewAttribute{ "_interpolation", "Linear" } );
        return inoutDocument.addChild( listKeyIndex, kKeyElementName, listAttribute );
    }
} // namespace sw::editor
