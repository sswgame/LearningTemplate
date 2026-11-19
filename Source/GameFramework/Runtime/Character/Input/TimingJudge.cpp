#include "pch.h"

#include "GameFramework/Runtime/Character/Input/TimingJudge.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

#include <algorithm>

namespace sw
{
    void TimingJudge::setWindows( const vector<TimingWindow>& listWindow )
    {
        _listWindow = listWindow;
        VectorUtil::stableSort( _listWindow, []( const TimingWindow& lhs, const TimingWindow& rhs )
        {
            return lhs._earlyWidth + lhs._lateWidth < rhs._earlyWidth + rhs._lateWidth;
        } );
    }

    bool TimingJudge::loadFromXML( const XMLNode& node, string_view sourceName, bool ( *pfnHandRead )( const utf8* pName ) )
    {
        // 적지 않은 칸은 TimingWindow 의 초기값이다. `width` 는 앞뒤 폭을 함께 정하고, `early` · `late` 가 각자 덮는다.
        // 깨진 값 · 음수 폭은 오류이고 창 표를 바꾸지 않는다(자르지 않는다).
        const auto readWindow = [&]( const XMLNode& child, TimingWindow& outWindow )
        {
            const bool bWidth    = XMLReadUtil::readOptionalNumber( child, "width", sourceName, 0.0f, MathUtil::kMaxFloat, outWindow._earlyWidth );
            outWindow._lateWidth = outWindow._earlyWidth;
            return XMLFields::readProperties( child, outWindow, sourceName, { "width" }, pfnHandRead ) && bWidth;
        };
        vector<TimingWindow> listWindow;
        const bool           bValid = GameDataXML::readChildrenInto( node, "Window", listWindow, readWindow );
        if ( bValid )
            setWindows( listWindow );
        return bValid;
    }

    bool TimingJudge::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, "TimingWindows", root ) == false )
            return false;
        return loadFromXML( root, sourceName ) && _listWindow.empty() == false;
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
