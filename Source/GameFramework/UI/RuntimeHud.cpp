#include "pch.h"

#include "GameFramework/UI/RuntimeHud.h"

#include "Engine/Utility/Debug/DebugOverlayState.h"

#include "GameFramework/Base/GameService.h"

namespace sw
{
    namespace
    {
        struct RuntimeHudInternal
        {
            static hashed_string keyFade()
            {
                static const hashed_string k{ "hud.fade" };
                return k;
            }

            static hashed_string keyAction()
            {
                static const hashed_string k{ "hud.action_mode" };
                return k;
            }

            static hashed_string keyDialogue()
            {
                static const hashed_string k{ "hud.dialogue" };
                return k;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RuntimeHud" );

    RuntimeHud::RuntimeHud()
        : _screen{}
        , _mapGauge{}
        , _dialogue{}
        , _fadeAlpha{ 0.0f }
        , _bVisible{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void RuntimeHud::setScreenRect( float32 x, float32 y, float32 w, float32 h )
    {
        _screen._x = x;
        _screen._y = y;
        _screen._w = w;
        _screen._h = h;
    }

    void RuntimeHud::setDialogue( string_view line )
    {
        if ( _dialogue == line )
            return;
        _dialogue.assign( line );
    }

    void RuntimeHud::clearDialogue()
    {
        _dialogue.clear();
    }

    void RuntimeHud::setGauge( const hashed_string& key, float32 fill, float32 x, float32 y, float32 w, float32 h )
    {
        _mapGauge[key] = HudGauge{ fill, x, y, w, h };
    }

    const HudGauge* RuntimeHud::getGauge( const hashed_string& key ) const
    {
        auto it = _mapGauge.find( key );
        if ( it != _mapGauge.end() )
            return &it->second;
        return nullptr;
    }

    float32 RuntimeHud::getGaugeFill( const hashed_string& key, float32 fallback ) const
    {
        const HudGauge* pGauge = getGauge( key );
        if ( pGauge != nullptr )
            return pGauge->_fill;
        return fallback;
    }

    void RuntimeHud::clearGauges()
    {
        _mapGauge.clear();
    }

    void RuntimeHud::setFadeOverlay( float32 alpha )
    {
        _fadeAlpha = alpha < 0.0f ? 0.0f : ( alpha > 1.0f ? 1.0f : alpha );
    }

    void RuntimeHud::publishSnapshot( bool actionMode ) const
    {
        DebugOverlayState* pOverlay = game::getService<DebugOverlayState>();
        if ( pOverlay == nullptr )
            return;

        pOverlay->_bVisible = _bVisible;
        pOverlay->setFloat( RuntimeHudInternal::keyFade(), _fadeAlpha );
        pOverlay->setFloat( RuntimeHudInternal::keyAction(), actionMode ? 1.0f : 0.0f );

        for ( const auto& [key, gauge] : _mapGauge )
        {
            pOverlay->setFloat( key, gauge._fill );
        }

        if ( _dialogue.empty() )
            pOverlay->setString( RuntimeHudInternal::keyDialogue(), {} );
        else
            pOverlay->setString( RuntimeHudInternal::keyDialogue(), _dialogue );
    }

    void RuntimeHud::logSnapshot() const
    {
        if ( _dialogue.empty() == false || _fadeAlpha > 0.01f )
        {
            SW_LOG_TRACE( "rect=(%#,%# %#x%#) fade=%# dlg='%#' gauges=%#",
                          _screen._x, _screen._y, _screen._w, _screen._h,
                          _fadeAlpha, _dialogue, static_cast<int32>( _mapGauge.size() ) );
        }
    }
} // namespace sw
