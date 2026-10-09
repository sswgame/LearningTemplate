#include "pch.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    void TimingJudge::setWindows( const vector<TimingWindow>& listWindow )
    {
        _listWindow = listWindow;
        std::stable_sort( _listWindow.begin(), _listWindow.end(), []( const TimingWindow& lhs, const TimingWindow& rhs )
        {
            return lhs._earlyWidth + lhs._lateWidth < rhs._earlyWidth + rhs._lateWidth;
        } );
    }

    void TimingJudge::loadFromNode( const XmlNode& node )
    {
        vector<TimingWindow> listWindow;
        for ( XmlNode child = node.findChild( "Window" ); child; child = child.findNextSibling( "Window" ) )
        {
            TimingWindow window;
            const utf8*  pGrade  = child.findAttribute( "grade" );
            window._grade        = pGrade != nullptr ? hashed_string( pGrade ) : hashed_string{};
            const float32 width  = child.getAttributeFloat( "width", window._earlyWidth );
            window._earlyWidth   = MathUtil::max( 0.0f, child.getAttributeFloat( "early", width ) );
            window._lateWidth    = MathUtil::max( 0.0f, child.getAttributeFloat( "late", width ) );
            window._score        = child.getAttributeInt( "score", 0 );
            window._bBreaksCombo = child.getAttributeBool( "breaksCombo", false ) ? SW_TRUE : SW_FALSE;
            listWindow.push_back( window );
        }
        setWindows( listWindow );
    }

    bool TimingJudge::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        if ( GameDataXml::parseRoot( doc, xmlText, sourceName, "TimingWindows", root ) == false )
            return false;
        loadFromNode( root );
        return _listWindow.empty() == false;
    }

    TimingResult TimingJudge::judge( float32 targetTime, float32 pressTime ) const
    {
        TimingResult result;
        result._offset = pressTime - _offset - targetTime;
        for ( const TimingWindow& window : _listWindow )
        {
            const bool bInside = result._offset < 0.0f ? -result._offset <= window._earlyWidth * _scale : result._offset <= window._lateWidth * _scale;
            if ( bInside )
            {
                result._pWindow = &window;
                break;
            }
        }
        return result;
    }

    bool TimingJudge::hasExpired( float32 targetTime, float32 now ) const
    {
        if ( _listWindow.empty() )
            return now - _offset > targetTime;
        return now - _offset - targetTime > _listWindow.back()._lateWidth * _scale;
    }

    float32 TimingJudge::getEarliestWidth() const { return _listWindow.empty() ? 0.0f : _listWindow.back()._earlyWidth * _scale; }
} // namespace sw
