#include "pch.h"

#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Object/Animation/AnimNotifyListener.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteAnimator" );

    // 컴포넌트는 오브젝트마다 만들어진다. 필드 크기 합(베이스 + 필드 + 비트필드 한 바이트)을 정렬로 올린 값을 넘으면 필드 사이에 구멍이 생긴 것이다.
    static_assert( sizeof( SpriteAnimatorComponent ) <=
                       ( sizeof( SceneComponent ) + sizeof( string ) * 2 + sizeof( AnimGraphAsset ) + sizeof( vector<string> ) + sizeof( float32 ) +
                         sizeof( SpriteAnimatorClipSource ) + sizeof( SpriteClipPlayable ) + sizeof( AnimGraphPlayer ) + sizeof( vector<AnimFiredNotify> ) +
                         sizeof( IAnimNotifyListener* ) + sizeof( int32 ) * 3 +
                         sizeof( const SpriteClipAsset* ) + sizeof( uint8 ) + alignof( SpriteAnimatorComponent ) - 1 ) /
                           alignof( SpriteAnimatorComponent ) * alignof( SpriteAnimatorComponent ),
                   "SpriteAnimatorComponent has padding between fields (or a field was added without adding its size here)" );

    SpriteAnimatorClipSource::SpriteAnimatorClipSource( SpriteAnimatorComponent& owner )
        : _owner{ owner }
    {
    }

    const IAnimPlayable* SpriteAnimatorClipSource::findPlayable( const hashed_string& name ) const
    {
        // 상태 기계가 넘어간 상태의 구간으로 하나뿐인 재생할 것을 맞춘다 — 스프라이트는 두 구간을 섞지 않는다(크로스페이드 0).
        (void)_owner.configurePlayable( string( name.c_str() ) );
        return &_owner._playable;
    }

    SpriteAnimatorComponent::SpriteAnimatorComponent()
        : _animGraphPath{}
        , _graph{}
        , _currentAnimation{}
        , _listAnimation{}
        , _frameRate{ 12.0f }
        , _clipSource{ *this }
        , _playable{}
        , _graphPlayer{}
        , _listFiredNotify{}
        , _pNotifyListener{ nullptr }
        , _currentFrame{ 0 }
        , _totalFrames{ 1 }
        , _pRangeClip{ nullptr }
        , _firstClipFrame{ 0 }
        , _bRepeat{ SW_FALSE }
        , _bPlaying{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bGraphLoaded{ SW_FALSE }
        , _bRootWarned{ SW_FALSE }
        , _bRangeChanged{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
        _graphPlayer.setPlayableSource( &_clipSource );
        _graphPlayer.setDefaultBlendSeconds( 0.0f );
    }

    void SpriteAnimatorComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );

        tryLoadAnimGraph();

        // 그래프가 없으면 클립의 이름 붙은 구간이 애니메이션 목록이다. 구간 이름이 없는 클립은 프레임 전체가 애니메이션 하나다.
        const SpriteClipAsset* pClip = findClip();
        if ( _listAnimation.empty() && pClip != nullptr )
        {
            for ( const SpriteClipAnimation& animation : pClip->_listAnimation )
                _listAnimation.push_back( animation._name );
        }

        if ( _listAnimation.empty() == false )
            play( _listAnimation[0], _bRepeat == SW_TRUE );
        else if ( pClip != nullptr && pClip->getFrameCount() > 0 )
            play( string{}, _bRepeat == SW_TRUE );
    }

    void SpriteAnimatorComponent::onEndPlay()
    {
        stop();
        Component::onEndPlay();
    }

    void SpriteAnimatorComponent::onTick( float32 deltaTime )
    {
        if ( _bPlaying == SW_FALSE || _bPaused == SW_TRUE )
            return;

        // 스프라이트의 클립이 바뀌었으면(늦게 붙었다 · 경로를 고쳤다) 구간을 다시 잡는다. 반복 여부는 재생을 시작할 때 정한 그대로다.
        if ( findClip() != _pRangeClip )
            (void)configurePlayable( _currentAnimation );

        // 시간 · 반복 · 끝 · "끝나면 다음" 은 상태 기계가 한다(스켈레탈 애니메이터와 같은 `AnimGraphPlayer`).
        _listFiredNotify.clear();
        _graphPlayer.update( deltaTime, nullptr, _pNotifyListener != nullptr ? &_listFiredNotify : nullptr );
        syncStateFromPlayer();
        if ( _pNotifyListener != nullptr )
        {
            // 워커다 — 받는 쪽(알림 디스패치)이 베껴 두고 틱 뒤 게임 스레드에서 처리한다.
            const IAnimPlayable* const arrActive[1] = { &_playable };
            AnimNotifyFrame            frame{};
            frame._listFired          = vector_reference<const AnimFiredNotify>{ _listFiredNotify.data(), _listFiredNotify.size() };
            frame._listActivePlayable = vector_reference<const IAnimPlayable* const>{ arrActive, 1 };
            frame._deltaSeconds       = deltaTime;
            frame._bFromTick          = SW_TRUE;
            frame._bRestarted         = _bRangeChanged;
            _bRangeChanged            = SW_FALSE;
            _pNotifyListener->onAnimNotifiesFired( frame );
        }

        const AnimPlayer& player    = _graphPlayer.getPlayer();
        const int32       prevFrame = _currentFrame;
        _currentFrame               = _playable.findFrameAtTime( player.getCurrentTime() );
        if ( player.hasFinished() )
        {
            // 반복하지 않는 구간이 끝났고 그래프에 다음이 없다 — 마지막 프레임에서 멈춘다.
            _currentFrame = _totalFrames - 1;
            _bPlaying     = SW_FALSE;
        }

        if ( _currentFrame != prevFrame )
            updateSpriteFrame();
        // 키는 프레임 사이에서도 보간된다 — 프레임이 그대로여도 매 틱 다시 읽는다.
        applyTransformKeys();
    }

    void SpriteAnimatorComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        static const hashed_string s_graphPathName( "_animGraphPath" );
        if ( propertyName != s_graphPathName )
            return;

        const float32 resumeTime  = _graphPlayer.getPlayer().getCurrentTime();
        const int32   resumeFrame = _currentFrame;
        tryLoadAnimGraph();
        if ( _bPlaying == SW_FALSE || _bGraphLoaded == SW_FALSE )
            return;
        // 지금 애니메이션이 새 그래프에도 있으면 그대로 잇는다. 없으면 그 이름은 이제 "끝나면 다음" 을 찾지 못한다 — 새 목록의 처음으로.
        if ( _graph.findNodeByName( _currentAnimation ) == nullptr && _listAnimation.empty() == false )
        {
            play( _listAnimation[0], _bRepeat == SW_TRUE );
            return;
        }
        // 그래프를 바꾸면 상태 기계가 멈춘다 — 같은 애니메이션을 같은 시각에서 다시 잇는다.
        play( _currentAnimation, _bRepeat == SW_TRUE );
        _graphPlayer.getPlayer().setCurrentTime( resumeTime );
        _currentFrame = MathUtil::clamp( resumeFrame, 0, _totalFrames - 1 );
        updateSpriteFrame();
    }

    void SpriteAnimatorComponent::play( const string& animName )
    {
        if ( _frameRate <= 0.0f )
            _frameRate = 12.0f;
        _currentAnimation = animName;
        _bPlaying         = SW_TRUE;
        _bPaused          = SW_FALSE;
        _currentFrame     = 0;
        _bRootWarned      = SW_FALSE;
        _bRepeat          = configurePlayable( animName ) ? SW_TRUE : SW_FALSE;
        // 구간은 방금 맞췄다 — 상태 기계에는 그 재생할 것을 바로 준다(이름이 그래프 노드면 그 노드의 "끝나면 다음" 을 따른다).
        _graphPlayer.playPlayable( hashed_string( animName ), &_playable, _bRepeat == SW_TRUE );
        updateSpriteFrame();
        applyTransformKeys();
    }

    void SpriteAnimatorComponent::play( const string& animName, bool loop )
    {
        play( animName );
        setRepeat( loop );
    }

    void SpriteAnimatorComponent::stop()
    {
        _bPlaying     = SW_FALSE;
        _bPaused      = SW_FALSE;
        _currentFrame = 0;
        _graphPlayer.getPlayer().setCurrentTime( 0.0f );
    }

    void SpriteAnimatorComponent::pause()
    {
        _bPaused = SW_TRUE;
    }

    void SpriteAnimatorComponent::resume()
    {
        _bPaused = SW_FALSE;
    }

    void SpriteAnimatorComponent::setFrame( int32 frame )
    {
        _currentFrame = MathUtil::clamp( frame, 0, MathUtil::max( _totalFrames - 1, 0 ) );
        _graphPlayer.getPlayer().setCurrentTime( _playable.computeFrameStart( _currentFrame ) );
        updateSpriteFrame();
        applyTransformKeys();
    }

    string SpriteAnimatorComponent::getCurrentAnimation() const
    {
        return _currentAnimation;
    }

    void SpriteAnimatorComponent::setCurrentAnimation( const string& anim )
    {
        _currentAnimation = anim;
    }

    bool SpriteAnimatorComponent::isRepeating() const
    {
        return _bRepeat == SW_TRUE;
    }

    void SpriteAnimatorComponent::setRepeat( bool bLoop )
    {
        _bRepeat = bLoop ? SW_TRUE : SW_FALSE;
        _graphPlayer.getPlayer().setCurrentLooping( bLoop );
    }

    float32 SpriteAnimatorComponent::getFrameRate() const
    {
        return _frameRate;
    }

    void SpriteAnimatorComponent::setFrameRate( float32 rate )
    {
        _frameRate = rate;
        // 시간이 없는 프레임의 길이가 바뀐다 — 구간 길이를 다시 센다.
        _playable.configure( _playable.getClip(), _playable.getFirstFrame(), _playable.getFrameCount(), _playable.isLoopingByDefault(), getFallbackFrameSeconds() );
    }

    int32 SpriteAnimatorComponent::getTotalFrames() const
    {
        return _totalFrames;
    }

    int32 SpriteAnimatorComponent::getCurrentFrame() const
    {
        return _currentFrame;
    }

    bool SpriteAnimatorComponent::isPlaying() const
    {
        return _bPlaying == SW_TRUE;
    }

    bool SpriteAnimatorComponent::isPaused() const
    {
        return _bPaused == SW_TRUE;
    }

    void SpriteAnimatorComponent::tryLoadAnimGraph()
    {
        _bGraphLoaded = SW_FALSE;
        _graphPlayer.setGraph( nullptr );
        _graph = AnimGraphAsset{};
        if ( _animGraphPath.empty() )
            return;
        if ( _graph.loadFromFile( _animGraphPath ) == false )
            return;
        _bGraphLoaded = SW_TRUE;
        _graphPlayer.setGraph( &_graph );
        _graph.collectNodeNames( _listAnimation );
    }

    void SpriteAnimatorComponent::syncStateFromPlayer()
    {
        const hashed_string& state = _graphPlayer.getCurrentStateName();
        if ( state.empty() || state.isEqual( hashed_string( _currentAnimation ), NameCase::CaseSensitive ) )
            return;
        // "끝나면 다음" 으로 넘어갔다 — 이름 · 반복(다음 상태는 반복하지 않는다)과 구간은 상태 기계가 풀어 둔 것을 따른다.
        _currentAnimation = state.c_str();
        _bRepeat          = _graphPlayer.getPlayer().isCurrentLooping() ? SW_TRUE : SW_FALSE;
        _bRootWarned      = SW_FALSE;
        _currentFrame     = -1; // 새 구간의 첫 프레임을 스프라이트에 넘기게 한다
    }

    SpriteComponent* SpriteAnimatorComponent::findSprite() const
    {
        GameObject* pGameObject = getOwner();
        return ( pGameObject != nullptr ) ? pGameObject->getComponent<SpriteComponent>() : nullptr;
    }

    const SpriteClipAsset* SpriteAnimatorComponent::findClip() const
    {
        const SpriteComponent* pSprite = findSprite();
        return ( pSprite != nullptr ) ? pSprite->getClip() : nullptr;
    }

    float32 SpriteAnimatorComponent::getFallbackFrameSeconds() const
    {
        return 1.0f / MathUtil::max( _frameRate, 1.0f );
    }

    bool SpriteAnimatorComponent::configurePlayable( const string& animName )
    {
        const SpriteClipAsset* pClip = findClip();
        _pRangeClip                  = pClip;
        _firstClipFrame              = 0;
        _totalFrames                 = 1;
        bool                bLoop    = true;
        SpriteClipAnimation range{};
        if ( pClip != nullptr )
        {
            if ( pClip->findFrameRange( animName, range ) )
            {
                _firstClipFrame = range._firstFrame;
                _totalFrames    = MathUtil::max( range._frameCount, 1 );
                bLoop           = range._bLoop == SW_TRUE;
            }
            else
            {
                SW_LOG_WARNING( "Sprite clip has no animation '%#' - showing frame 0", animName );
            }
        }
        _playable.configure( pClip, _firstClipFrame, _totalFrames, bLoop, getFallbackFrameSeconds() );
        _currentFrame  = MathUtil::clamp( _currentFrame, 0, _totalFrames - 1 );
        _bRangeChanged = SW_TRUE;
        return bLoop;
    }

    void SpriteAnimatorComponent::updateSpriteFrame()
    {
        SpriteComponent* pSprite = findSprite();
        if ( pSprite == nullptr )
            return;
        pSprite->setClipFrame( _firstClipFrame + MathUtil::max( _currentFrame, 0 ) );
    }

    float32 SpriteAnimatorComponent::computeClipTime() const
    {
        return _playable.computeClipTime( _graphPlayer.getPlayer().getCurrentTime() );
    }

    void SpriteAnimatorComponent::applyTransformKeys()
    {
        const SpriteClipAsset* pClip = findClip();
        if ( pClip == nullptr || pClip->hasTransformKeys() == false )
            return;
        SpriteComponent* pSprite = findSprite();
        if ( pSprite == nullptr )
            return;

        const GameObject* pOwner        = getOwner();
        const bool        bSpriteIsRoot = ( pOwner != nullptr ) && ( pOwner->getPrimarySceneComponent() == pSprite );
        if ( bSpriteIsRoot )
        {
            if ( _bRootWarned == SW_FALSE )
            {
                _bRootWarned = SW_TRUE;
                SW_LOG_WARNING( "'%#': the sprite clip has transform keys but the sprite is the object's root - keys are not applied; attach the sprite under a root",
                                pOwner->getName().c_str() );
            }
            return;
        }

        SpriteClipKey key{};
        if ( pClip->sampleTransformKey( computeClipTime(), key ) == false )
            return;
        const float3 localPosition = pSprite->getLocalPosition();
        float3       localRotation = pSprite->getLocalRotation();
        localRotation._z           = MathUtil::toRadian( key._angleDeg );
        pSprite->setLocalPosition( float3{ key._position._x, key._position._y, localPosition._z } );
        pSprite->setLocalRotation( localRotation );
    }
} // namespace sw
