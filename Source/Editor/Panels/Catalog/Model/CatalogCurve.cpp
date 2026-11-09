#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogCurve.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

#include "Engine/Reflection/EnumName.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct CatalogCurveModelInternal
        {
            static constexpr float32 kNewKeyTimeStep   = 0.1f;
            static constexpr float32 kNewKeyFirstValue = 1.0f;

            /** @brief 이름이 @p name 인 첫 자식 원소입니다. 없으면 비어 있습니다. */
            static optional<int32> findChildNamed( const CatalogXMLText& parsed, const CatalogXMLElement& element, string_view name )
            {
                for ( const int32 childIndex : element._listChild )
                {
                    if ( string_view{ parsed.getElement( childIndex )._name } == name )
                        return childIndex;
                }
                return nullopt;
            }

            /** @brief 보간의 파일 이름입니다(`makeFloatCurve` 가 읽는 철자 — 리플렉션 열거자 이름). */
            [[nodiscard]] static string getInterpolationName( CurveInterpolation interpolation )
            {
                return string{ getEnumName( interpolation ) };
            }

            static bool isGradientChannel( string_view name )
            {
                return name == "_red" || name == "_green" || name == "_blue" || name == "_alpha";
            }

            /** @brief 속성 값을 실수로 읽습니다. 없거나 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool readFloat( const CatalogXMLElement& element, string_view name, float32& outValue )
            {
                const optional<uint32> attributeIndex = element.findAttribute( name );
                if ( attributeIndex.has_value() == false )
                    return false;
                return StringUtil::parseFloat( element._listAttribute[*attributeIndex]._value, outValue );
            }

            static string makeSegment( const CatalogXMLElement& element )
            {
                string                 segment   = element._name;
                const optional<uint32> nameIndex = element.findAttribute( "_name" );
                if ( nameIndex.has_value() )
                {
                    segment += "(";
                    segment += element._listAttribute[*nameIndex]._value;
                    segment += ")";
                }
                return segment;
            }

            static void readKeys( const CatalogXMLText& parsed, const CatalogXMLElement& listKey, vector<CatalogCurveKey>& outListKey )
            {
                for ( const int32 keyIndex : listKey._listChild )
                {
                    const CatalogXMLElement& keyElement = parsed.getElement( keyIndex );
                    if ( string_view{ keyElement._name } != CatalogCurve::kKeyElementName )
                        continue;
                    CatalogCurveKey key;
                    key._elementIndex                         = keyIndex;
                    const bool bTime                          = readFloat( keyElement, "_time", key._time );
                    const bool bValue                         = readFloat( keyElement, "_value", key._value );
                    key._bValid                               = bTime && bValue;
                    const optional<uint32> interpolationIndex = keyElement.findAttribute( "_interpolation" );
                    if ( interpolationIndex.has_value() )
                        key._interpolation = keyElement._listAttribute[*interpolationIndex]._value;
                    outListKey.push_back( std::move( key ) );
                }
            }

            static void walk( const CatalogXMLText& parsed, int32 elementIndex, const string& prefix, vector<CatalogCurve>& outListCurve )
            {
                const CatalogXMLElement& element        = parsed.getElement( elementIndex );
                bool                     bHasCurveChild = false;
                for ( const int32 childIndex : element._listChild )
                {
                    bHasCurveChild = bHasCurveChild || findChildNamed( parsed, parsed.getElement( childIndex ), CatalogCurve::kKeyListName ).has_value();
                }
                for ( const int32 childIndex : element._listChild )
                {
                    const CatalogXMLElement& child = parsed.getElement( childIndex );
                    if ( string_view{ child._name } == CatalogCurve::kKeyListName )
                        continue;
                    const string          label        = prefix.empty() ? makeSegment( child ) : prefix + "/" + makeSegment( child );
                    const optional<int32> listKeyIndex = findChildNamed( parsed, child, CatalogCurve::kKeyListName );
                    // 키가 없는 채널(`<_red/>`)은 모양만으로는 곡선인지 알 수 없다 - 형제에 곡선이 있는 색 채널 이름일 때만 빈 곡선으로 본다.
                    const bool bEmptyChannel = child._listChild.empty() && bHasCurveChild && isGradientChannel( child._name );
                    if ( listKeyIndex.has_value() || bEmptyChannel )
                    {
                        CatalogCurve curve;
                        curve._label        = label;
                        curve._channel      = child._name;
                        curve._elementIndex = childIndex;
                        curve._parentIndex  = elementIndex;
                        if ( listKeyIndex.has_value() )
                            readKeys( parsed, parsed.getElement( *listKeyIndex ), curve._listKey );
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
    void CatalogCurve::collectCurves( const CatalogXMLText& parsed, int32 ownerElement, vector<CatalogCurve>& outListCurve )
    {
        outListCurve.clear();
        const bool bInvalid = parsed.isParsed() == false || ownerElement < 0 || static_cast<size_t>( ownerElement ) >= parsed.getElements().size();
        if ( bInvalid )
            return;
        CatalogCurveModelInternal::walk( parsed, ownerElement, string{}, outListCurve );
    }

    void CatalogCurve::collectGradients( const vector<CatalogCurve>& listCurve, vector<CatalogGradient>& outListGradient )
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

    FloatCurve CatalogCurve::makeFloatCurve( const CatalogCurve& curve )
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

    float32 CatalogCurve::computeEndTime( const CatalogCurve& curve )
    {
        float32 endTime = 1.0f;
        for ( const CatalogCurveKey& key : curve._listKey )
        {
            if ( key._bValid && key._time > endTime )
                endTime = key._time;
        }
        return endTime;
    }

    bool CatalogCurve::sample( const CatalogCurve& curve, float32 endTime, uint32 sampleCount, vector<float32>& outListValue )
    {
        outListValue.clear();
        if ( sampleCount < 2 )
            return false;
        const FloatCurve floatCurve = makeFloatCurve( curve );
        if ( floatCurve._listKey.empty() )
            return false;
        for ( uint32 index = 0; index < sampleCount; ++index )
        {
            const float32           time  = endTime * static_cast<float32>( index ) / static_cast<float32>( sampleCount - 1 );
            const optional<float32> value = floatCurve.evaluate( time );
            outListValue.push_back( value.value_or( 0.0f ) ); // 키가 있으면 값이 있다(위에서 거른다)
        }
        return true;
    }

    bool CatalogCurve::sampleGradient( const vector<CatalogCurve>& listCurve, const CatalogGradient& gradient, uint32 sampleCount, vector<float4>& outListColor )
    {
        outListColor.clear();
        const CatalogCurve& red     = listCurve[static_cast<size_t>( gradient._red )];
        const CatalogCurve& green   = listCurve[static_cast<size_t>( gradient._green )];
        const CatalogCurve& blue    = listCurve[static_cast<size_t>( gradient._blue )];
        const CatalogCurve& alpha   = listCurve[static_cast<size_t>( gradient._alpha )];
        const float32       endTime = MathUtil::max( MathUtil::max( computeEndTime( red ), computeEndTime( green ) ), MathUtil::max( computeEndTime( blue ), computeEndTime( alpha ) ) );
        vector<float32>     listRed;
        vector<float32>     listGreen;
        vector<float32>     listBlue;
        vector<float32>     listAlpha;
        const bool          bSampled = sample( red, endTime, sampleCount, listRed ) && sample( green, endTime, sampleCount, listGreen ) && sample( blue, endTime, sampleCount, listBlue ) && sample( alpha, endTime, sampleCount, listAlpha );
        if ( bSampled == false )
            return false;
        for ( uint32 index = 0; index < sampleCount; ++index )
        {
            outListColor.push_back( float4{ MathUtil::saturate( listRed[index] ), MathUtil::saturate( listGreen[index] ), MathUtil::saturate( listBlue[index] ), MathUtil::saturate( listAlpha[index] ) } );
        }
        return true;
    }

    bool CatalogCurve::addKey( CatalogDocument& inoutDocument, int32 curveElementIndex )
    {
        using Internal                             = CatalogCurveModelInternal;
        const CatalogXMLText& parsed               = inoutDocument.getParsed();
        const bool            bInvalidElementIndex = parsed.isParsed() == false || curveElementIndex < 0 || static_cast<size_t>( curveElementIndex ) >= parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            inoutDocument.setError( "the curve element does not exist" );
            return false;
        }
        CatalogDocument draft;
        (void)draft.load( inoutDocument.getText() ); // 위에서 훑어진 글이다
        optional<int32> listKeyIndex = Internal::findChildNamed( draft.getParsed(), draft.getParsed().getElement( curveElementIndex ), kKeyListName );
        if ( listKeyIndex.has_value() == false )
        {
            if ( draft.addChild( curveElementIndex, kKeyListName, {} ) == false )
            {
                inoutDocument.setError( draft.getError() );
                return false;
            }
            // 고치면 문서가 다시 훑여 인덱스가 바뀐다 - 곡선 원소는 더한 원소보다 앞이라 같은 인덱스로 남는다. 방금 더한 자식이므로 반드시 찾는다.
            listKeyIndex = Internal::findChildNamed( draft.getParsed(), draft.getParsed().getElement( curveElementIndex ), kKeyListName );
        }

        // 새 키의 시각과 값: 마지막 키 뒤 한 걸음에 같은 값, 키가 없으면 시각 0 값 1.
        float32                  time    = 0.0f;
        float32                  value   = Internal::kNewKeyFirstValue;
        const CatalogXMLElement& listKey = draft.getParsed().getElement( listKeyIndex.value() ); // 위에서 더했으면 반드시 있다
        for ( const int32 keyIndex : listKey._listChild )
        {
            const CatalogXMLElement& keyElement = draft.getParsed().getElement( keyIndex );
            float32                  keyTime    = 0.0f;
            float32                  keyValue   = 0.0f;
            const bool               bRead      = Internal::readFloat( keyElement, "_time", keyTime ) && Internal::readFloat( keyElement, "_value", keyValue );
            if ( bRead && keyTime + Internal::kNewKeyTimeStep > time )
            {
                time  = keyTime + Internal::kNewKeyTimeStep;
                value = keyValue;
            }
        }
        StringBuilder<constant::buffer::kMax32> timeText;
        StringBuilder<constant::buffer::kMax32> valueText;
        timeText.append( time );
        valueText.append( value );
        vector<CatalogXMLNewAttribute> listAttribute;
        listAttribute.push_back( CatalogXMLNewAttribute{ "_time", string{ timeText.view() } } );
        listAttribute.push_back( CatalogXMLNewAttribute{ "_value", string{ valueText.view() } } );
        listAttribute.push_back( CatalogXMLNewAttribute{ "_interpolation", "Linear" } );
        if ( draft.addChild( listKeyIndex, kKeyElementName, listAttribute ) == false )
        {
            inoutDocument.setError( draft.getError() );
            return false;
        }
        return inoutDocument.replaceTextChecked( draft.getText() );
    }

    bool CatalogCurve::applyEditedCurve( CatalogDocument& inoutDocument, int32 curveElementIndex, const FloatCurve& edited )
    {
        using Internal                      = CatalogCurveModelInternal;
        const CatalogXMLText& parsed        = inoutDocument.getParsed();
        const bool            bInvalidCurve = parsed.isParsed() == false || curveElementIndex < 0 || static_cast<size_t>( curveElementIndex ) >= parsed.getElements().size();
        if ( bInvalidCurve )
        {
            inoutDocument.setError( "the curve element does not exist" );
            return false;
        }
        // 곡선의 키는 부모 아래 곡선 모으기와 같은 길로 읽는다(읽지 못한 키 표시까지 같다).
        vector<CatalogCurve> listSibling;
        collectCurves( parsed, parsed.getElement( curveElementIndex )._parent, listSibling );
        const CatalogCurve* pCurve = nullptr;
        for ( const CatalogCurve& sibling : listSibling )
        {
            if ( sibling._elementIndex == curveElementIndex )
                pCurve = &sibling;
        }
        if ( pCurve == nullptr )
        {
            inoutDocument.setError( "the element is not a curve (it has no _listKey)" );
            return false;
        }
        const CatalogCurve& curve = *pCurve;
        // 읽은 키를 시각 순으로 — `makeFloatCurve` 가 편집기에 준 순서와 같다(같은 시각은 문서 순서).
        vector<const CatalogCurveKey*> listReadKey;
        for ( const CatalogCurveKey& key : curve._listKey )
        {
            if ( key._bValid )
                listReadKey.push_back( &key );
        }
        std::stable_sort( listReadKey.begin(), listReadKey.end(), []( const CatalogCurveKey* pLeft, const CatalogCurveKey* pRight )
        { return pLeft->_time < pRight->_time; } );

        CatalogDocument draft;
        (void)draft.load( inoutDocument.getText() ); // 위에서 훑어진 글이다
        const auto writeNumber = []( float32 value )
        {
            StringBuilder<constant::buffer::kMax32> text;
            text.append( value );
            return string{ text.view() };
        };
        const size_t sharedCount = MathUtil::min( listReadKey.size(), edited._listKey.size() );
        for ( size_t keyIndex = 0; keyIndex < sharedCount; ++keyIndex )
        {
            // 칸 값만 고치므로 원소 번호는 그대로다.
            const FloatCurveKey& key     = edited._listKey[keyIndex];
            const int32          element = listReadKey[keyIndex]->_elementIndex;
            const bool           bSet    = draft.setCell( element, "_time", writeNumber( key._time ) ) && draft.setCell( element, "_value", writeNumber( key._value ) ) &&
                              draft.setCell( element, "_interpolation", Internal::getInterpolationName( key._interpolation ) );
            if ( bSet == false )
            {
                inoutDocument.setError( draft.getError() );
                return false;
            }
        }
        if ( edited._listKey.size() > listReadKey.size() )
        {
            optional<int32> listKeyIndex = Internal::findChildNamed( draft.getParsed(), draft.getParsed().getElement( curve._elementIndex ), kKeyListName );
            if ( listKeyIndex.has_value() == false && draft.addChild( curve._elementIndex, kKeyListName, {} ) )
                listKeyIndex = Internal::findChildNamed( draft.getParsed(), draft.getParsed().getElement( curve._elementIndex ), kKeyListName );
            for ( size_t keyIndex = sharedCount; keyIndex < edited._listKey.size() && listKeyIndex.has_value(); ++keyIndex )
            {
                const FloatCurveKey&           key = edited._listKey[keyIndex];
                vector<CatalogXMLNewAttribute> listAttribute;
                listAttribute.push_back( CatalogXMLNewAttribute{ "_time", writeNumber( key._time ) } );
                listAttribute.push_back( CatalogXMLNewAttribute{ "_value", writeNumber( key._value ) } );
                listAttribute.push_back( CatalogXMLNewAttribute{ "_interpolation", Internal::getInterpolationName( key._interpolation ) } );
                // 더한 키는 목록 끝이다 — 곡선 · 목록 원소는 그보다 앞이라 번호가 그대로다.
                if ( draft.addChild( *listKeyIndex, kKeyElementName, listAttribute ) == false )
                {
                    inoutDocument.setError( draft.getError() );
                    return false;
                }
            }
            if ( listKeyIndex < 0 )
            {
                inoutDocument.setError( draft.getError() );
                return false;
            }
        }
        // 남는 키는 뒤 원소부터 지운다 — 앞 원소의 번호가 당겨지지 않는다.
        vector<int32> listRemoved;
        for ( size_t keyIndex = sharedCount; keyIndex < listReadKey.size(); ++keyIndex )
        {
            listRemoved.push_back( listReadKey[keyIndex]->_elementIndex );
        }
        std::sort( listRemoved.begin(), listRemoved.end(), []( int32 left, int32 right )
        { return left > right; } );
        for ( const int32 element : listRemoved )
        {
            if ( draft.removeRow( element ) == false )
            {
                inoutDocument.setError( draft.getError() );
                return false;
            }
        }
        if ( draft.getText() == inoutDocument.getText() )
            return false; // 같은 자리에 놓았다 — 되돌리기를 쌓지 않는다
        return inoutDocument.replaceTextChecked( draft.getText() );
    }
} // namespace sw::editor
