#include "pch.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

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

    bool TimingJudge::loadFromNode( const XMLNode& node, string_view sourceName )
    {
        // 적지 않은 칸은 TimingWindow 의 초기값이다. `width` 는 앞뒤 폭을 함께 정하고, `early` · `late` 가 각자 덮는다.
        // 깨진 값 · 음수 폭은 오류이고 창 표를 바꾸지 않는다(자르지 않는다).
        constexpr float32    kAnyFloat = MathUtil::kMaxFloat;
        bool                 bValid    = true;
        vector<TimingWindow> listWindow;
        for ( XMLNode child = node.findChild( "Window" ); child; child = child.findNextSibling( "Window" ) )
        {
            TimingWindow window;
            window._grade      = GameDataXML::readName( child, "grade" );
            float32 width      = window._earlyWidth;
            bValid             = GameDataXML::readOptionalNumber( child, "width", sourceName, 0.0f, kAnyFloat, width ) && bValid;
            window._earlyWidth = width;
            window._lateWidth  = width;
            bValid             = GameDataXML::readOptionalNumber( child, "early", sourceName, 0.0f, kAnyFloat, window._earlyWidth ) && bValid;
            bValid             = GameDataXML::readOptionalNumber( child, "late", sourceName, 0.0f, kAnyFloat, window._lateWidth ) && bValid;
            bValid             = GameDataXML::readOptionalNumber( child, "score", sourceName, MathUtil::kMinInt32, MathUtil::kMaxInt32, window._score ) && bValid;
            bValid             = GameDataXML::readOptionalFlag( child, "breaksCombo", window._bBreaksCombo ) && bValid;
            listWindow.push_back( window );
        }
        if ( bValid )
            setWindows( listWindow );
        return bValid;
    }

    bool TimingJudge::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( GameDataXML::parseRoot( doc, xmlText, sourceName, "TimingWindows", root ) == false )
            return false;
        return loadFromNode( root, sourceName ) && _listWindow.empty() == false;
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

    float32 TimingJudge::getEarliestWidth() const
    {
        return _listWindow.empty() ? 0.0f : _listWindow.back()._earlyWidth * _scale;
    }
} // namespace sw
