#include "pch.h"

#include "GameFramework/Base/Data/GameCurve.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    void GameCurve::addPoint( float32 time, float32 value )
    {
        GameCurvePoint point;
        point._time        = time;
        point._value       = value;
        size_t insertIndex = _listPoint.size();
        while ( insertIndex > 0 && _listPoint[insertIndex - 1]._time > time )
            --insertIndex;
        _listPoint.insert( _listPoint.begin() + static_cast<ptrdiff_t>( insertIndex ), point );
    }

    uint32 GameCurve::readPoints( const XmlNode& node, const utf8* pElement, const utf8* pValueAttribute, float32 minValue )
    {
        uint32 readCount = 0;
        for ( XmlNode child = node.findChild( pElement ); child; child = child.findNextSibling( pElement ) )
        {
            addPoint( child.getAttributeFloat( "time", 0.0f ), MathUtil::max( minValue, child.getAttributeFloat( pValueAttribute, 1.0f ) ) );
            ++readCount;
        }
        return readCount;
    }

    float32 GameCurve::evaluate( float32 time, float32 fallback ) const
    {
        if ( _listPoint.empty() )
            return fallback;
        if ( time <= _listPoint.front()._time )
            return _listPoint.front()._value;
        for ( size_t index = 1; index < _listPoint.size(); ++index )
        {
            const GameCurvePoint& next = _listPoint[index];
            if ( time > next._time )
                continue;
            const GameCurvePoint& previous = _listPoint[index - 1];
            const float32         span     = next._time - previous._time;
            const float32         alpha    = span > 0.0f ? ( time - previous._time ) / span : 1.0f;
            return previous._value + ( next._value - previous._value ) * alpha;
        }
        return _listPoint.back()._value;
    }
} // namespace sw
