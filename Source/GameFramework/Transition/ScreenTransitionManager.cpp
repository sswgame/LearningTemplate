#include "pch.h"

#include "GameFramework/Transition/ScreenTransitionManager.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ScreenTransition" );

    // ------------------------------------------------------------------------------
    // 1) FadeService
    // ------------------------------------------------------------------------------

    FadeService::FadeService()
        : _duration{ 0.35f }
        , _elapsed{ 0.0f }
        , _alpha{ 0.0f }
        , _phase{ FadePhase::Idle }
        , _bFinished{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void FadeService::beginFadeOut( float32 duration )
    {
        _phase     = FadePhase::FadingOut;
        _duration  = MathUtil::max( duration, 0.001f );
        _elapsed   = 0.0f;
        _alpha     = 0.0f;
        _bFinished = SW_FALSE;
    }

    void FadeService::beginFadeIn( float32 duration )
    {
        _phase     = FadePhase::FadingIn;
        _duration  = MathUtil::max( duration, 0.001f );
        _elapsed   = 0.0f;
        _alpha     = 1.0f;
        _bFinished = SW_FALSE;
    }

    void FadeService::update( float32 deltaTime )
    {
        if ( _phase == FadePhase::Idle )
            return;

        _elapsed += deltaTime;
        const float32 t = MathUtil::saturate( _elapsed / _duration );

        if ( _phase == FadePhase::FadingOut )
        {
            _alpha = t;
            if ( t >= 1.0f )
            {
                _phase     = FadePhase::HoldBlack;
                _alpha     = 1.0f;
                _bFinished = SW_TRUE;
            }
        }
        else if ( _phase == FadePhase::FadingIn )
        {
            _alpha = 1.0f - t;
            if ( t >= 1.0f )
            {
                _phase     = FadePhase::Idle;
                _alpha     = 0.0f;
                _bFinished = SW_TRUE;
            }
        }
        else if ( _phase == FadePhase::HoldBlack )
            _alpha = 1.0f;
    }

    bool FadeService::isFinished() const
    {
        return _bFinished == SW_TRUE;
    }

    // ------------------------------------------------------------------------------
    // 2) ScreenTransitionManager
    // ------------------------------------------------------------------------------

    ScreenTransitionManager::ScreenTransitionManager()
        : _fade{}
        , _callbacks{}
        , _pendingAction{}
        , _pendingFadeInDuration{ 0.35f }
        , _phase{ Phase::None }
        , _arrReserved{ 0 }
    {
    }

    void ScreenTransitionManager::beginTransition( Delegate<void()> onExecute, float32 fadeOutDuration, float32 fadeInDuration )
    {
        _pendingAction         = onExecute;
        _pendingFadeInDuration = fadeInDuration;
        _phase                 = Phase::FadeOut;

        if ( _callbacks.onTransitionStarted.isBound() == true )
            _callbacks.onTransitionStarted();

        if ( _callbacks.setPlayerInputEnabled.isBound() == true )
            _callbacks.setPlayerInputEnabled( false );

        _fade.beginFadeOut( fadeOutDuration );
        SW_LOG_INFO( "Begin ScreenTransition (FadeOut duration=%#s)", fadeOutDuration );
    }

    void ScreenTransitionManager::update( float32 deltaTime )
    {
        _fade.update( deltaTime );

        if ( _phase == Phase::FadeOut )
        {
            if ( _fade.getPhase() == FadePhase::HoldBlack )
            {
                _phase = Phase::Loading;
                SW_LOG_TRACE( "ScreenTransition: FadeOut complete, executing transition action." );

                if ( _pendingAction.isBound() == true )
                    _pendingAction();

                _phase = Phase::FadeIn;
                _fade.beginFadeIn( _pendingFadeInDuration );
            }
        }
        else if ( _phase == Phase::FadeIn )
        {
            if ( _fade.getPhase() == FadePhase::Idle )
            {
                _phase = Phase::None;
                SW_LOG_INFO( "ScreenTransition: Transition complete, restoring player input." );

                if ( _callbacks.setPlayerInputEnabled.isBound() == true )
                    _callbacks.setPlayerInputEnabled( true );

                if ( _callbacks.onTransitionFinished.isBound() == true )
                    _callbacks.onTransitionFinished();
            }
        }
    }

    void ScreenTransitionManager::reset()
    {
        _phase         = Phase::None;
        _pendingAction = nullptr;
        _fade          = FadeService{};
    }
} // namespace sw
