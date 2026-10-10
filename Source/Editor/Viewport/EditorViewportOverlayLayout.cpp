#include "pch.h"

#include "Editor/Viewport/EditorViewportOverlayLayout.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportOverlayLayoutInternal
        {
            /** @brief 바가 놓일 수 있는 남는 자리(뷰포트 − 바, 음수면 0)입니다. */
            static float2 computeFreeSpace( const float2& barSize, const float2& viewSize )
            {
                return float2{ MathUtil::max( viewSize._x - barSize._x, 0.0f ), MathUtil::max( viewSize._y - barSize._y, 0.0f ) };
            }

            static string formatFraction( float32 value )
            {
                fixed_string<constant::kMaxBuffer32> text;
                formatstring( text.data(), text.capacity(), "%.4f", static_cast<float64>( value ) );
                return string{ text.c_str() };
            }

            static float32 toFraction( float32 position, float32 freeSpace ) { return freeSpace > 0.0f ? MathUtil::saturate( position / freeSpace ) : 0.0f; }

            static const utf8* getDockName( EditorOverlayDock dock )
            {
                constexpr const utf8* kArrName[] = { "free", "top", "bottom", "left", "right" };
                return kArrName[static_cast<uint32>( dock )];
            }

            [[nodiscard]] static bool parseDock( string_view text, EditorOverlayDock& outDock )
            {
                for ( uint32 index = 0; index <= static_cast<uint32>( EditorOverlayDock::Right ); ++index )
                {
                    if ( text == getDockName( static_cast<EditorOverlayDock>( index ) ) )
                    {
                        outDock = static_cast<EditorOverlayDock>( index );
                        return true;
                    }
                }
                return false;
            }
        };
    } // namespace

    void EditorViewportOverlayLayout::registerBar( const EditorOverlayBarState& defaultState )
    {
        bool bDefaultKnown = false;
        for ( EditorOverlayBarState& known : _listDefault )
        {
            if ( known._id == defaultState._id )
            {
                known         = defaultState;
                bDefaultKnown = true;
            }
        }
        if ( bDefaultKnown == false )
            _listDefault.push_back( defaultState );
        if ( findBar( defaultState._id ) == nullptr )
            _listBar.push_back( defaultState );
    }

    EditorOverlayBarState* EditorViewportOverlayLayout::findBar( string_view barID )
    {
        for ( EditorOverlayBarState& bar : _listBar )
        {
            if ( bar._id == barID )
                return &bar;
        }
        return nullptr;
    }

    const EditorOverlayBarState* EditorViewportOverlayLayout::findBar( string_view barID ) const
    {
        for ( const EditorOverlayBarState& bar : _listBar )
        {
            if ( bar._id == barID )
                return &bar;
        }
        return nullptr;
    }

    void EditorViewportOverlayLayout::resetToDefault()
    {
        _listBar = _listDefault;
    }

    float2 EditorViewportOverlayLayout::computePosition( const EditorOverlayBarState& state, const float2& barSize, const float2& viewSize )
    {
        const float2 freeSpace = EditorViewportOverlayLayoutInternal::computeFreeSpace( barSize, viewSize );
        float2       fraction  = float2{ MathUtil::saturate( state._fraction._x ), MathUtil::saturate( state._fraction._y ) };
        switch ( state._dock )
        {
            case EditorOverlayDock::Top:
            {
                fraction._y = 0.0f;
                break;
            }
            case EditorOverlayDock::Bottom:
            {
                fraction._y = 1.0f;
                break;
            }
            case EditorOverlayDock::Left:
            {
                fraction._x = 0.0f;
                break;
            }
            case EditorOverlayDock::Right:
            {
                fraction._x = 1.0f;
                break;
            }
            case EditorOverlayDock::Free:
            {
                break;
            }
        }
        return float2{ freeSpace._x * fraction._x, freeSpace._y * fraction._y };
    }

    void EditorViewportOverlayLayout::applyDrop( EditorOverlayBarState& state, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance )
    {
        const float2  freeSpace = EditorViewportOverlayLayoutInternal::computeFreeSpace( barSize, viewSize );
        const float32 x         = MathUtil::clamp( position._x, 0.0f, freeSpace._x );
        const float32 y         = MathUtil::clamp( position._y, 0.0f, freeSpace._y );
        state._fraction         = float2{ EditorViewportOverlayLayoutInternal::toFraction( x, freeSpace._x ), EditorViewportOverlayLayoutInternal::toFraction( y, freeSpace._y ) };
        // 가장 가까운 가장자리 하나에 붙인다(거리가 같으면 위 · 아래가 먼저 — 가로 바가 흔하다).
        const float32 arrDistance[] = { y, freeSpace._y - y, x, freeSpace._x - x };
        uint32        nearest       = 0;
        for ( uint32 index = 1; index < 4; ++index )
        {
            if ( arrDistance[index] < arrDistance[nearest] )
                nearest = index;
        }
        state._dock = arrDistance[nearest] <= snapDistance ? static_cast<EditorOverlayDock>( nearest + 1 ) : EditorOverlayDock::Free;
    }

    bool EditorViewportOverlayLayout::isVertical( const EditorOverlayBarState& state )
    {
        return state._dock == EditorOverlayDock::Left || state._dock == EditorOverlayDock::Right;
    }

    void EditorViewportOverlayLayout::writeTo( string_view prefix, KeyValueMap& outMap ) const
    {
        for ( const EditorOverlayBarState& bar : _listBar )
        {
            const string key          = string( prefix ) + "." + bar._id + ".";
            outMap[key + "dock"]      = EditorViewportOverlayLayoutInternal::getDockName( bar._dock );
            outMap[key + "x"]         = EditorViewportOverlayLayoutInternal::formatFraction( bar._fraction._x );
            outMap[key + "y"]         = EditorViewportOverlayLayoutInternal::formatFraction( bar._fraction._y );
            outMap[key + "collapsed"] = bar._bCollapsed ? "true" : "false";
            outMap[key + "visible"]   = bar._bVisible ? "true" : "false";
        }
    }

    void EditorViewportOverlayLayout::readFrom( string_view prefix, const KeyValueMap& mapData )
    {
        const string head = string( prefix ) + ".";
        for ( const auto& [key, value] : mapData )
        {
            if ( StringUtil::startsWith( key, head ) == false )
                continue;
            const string_view rest = string_view{ key }.substr( head.size() );
            const size_t      dot  = rest.rfind( '.' );
            if ( dot == string_view::npos || dot == 0 )
                continue;
            const string           barID{ rest.substr( 0, dot ) };
            const string_view      field = rest.substr( dot + 1 );
            EditorOverlayBarState* pBar  = findBar( barID );
            if ( pBar == nullptr )
            {
                EditorOverlayBarState loaded{};
                loaded._id = barID;
                _listBar.push_back( loaded );
                pBar = &_listBar.back();
            }
            float64 number{ 0.0 };
            if ( field == "dock" )
                (void)EditorViewportOverlayLayoutInternal::parseDock( value, pBar->_dock ); // 모르는 이름이면 그대로 둔다
            else if ( field == "x" && StringUtil::parseDouble( value, number ) )
                pBar->_fraction._x = MathUtil::saturate( static_cast<float32>( number ) );
            else if ( field == "y" && StringUtil::parseDouble( value, number ) )
                pBar->_fraction._y = MathUtil::saturate( static_cast<float32>( number ) );
            else if ( field == "collapsed" )
                pBar->_bCollapsed = value == "true";
            else if ( field == "visible" )
                pBar->_bVisible = value != "false";
        }
    }
} // namespace sw::editor
