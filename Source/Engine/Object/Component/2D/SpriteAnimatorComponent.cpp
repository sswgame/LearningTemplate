#include "pch.h"

#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteAnimator" );

    SpriteAnimatorComponent::SpriteAnimatorComponent()
        : _animationGraphPath{}
        , _graph{}
        , _currentAnimation{}
        , _listAnimation{}
        , _frameRate{ 12.0f }
        , _frameTimer{ 0.0f }
        , _currentFrame{ 0 }
        , _totalFrames{ 1 }
        , _firstClipFrame{ 0 }
        , _pRangeClip{ nullptr }
        , _bRepeat{ SW_FALSE }
        , _bPlaying{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bGraphLoaded{ SW_FALSE }
        , _bRootWarned{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void SpriteAnimatorComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );

        tryLoadAnimationGraph();

        // 그래프가 없으면 클립의 이름 붙은 구간이 애니메이션 목록이다. 이름 없는 옛 클립은 프레임 전체가 애니메이션 하나다.
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
            resolveFrameRange( false );
        if ( _totalFrames <= 0 )
            return;

        _frameTimer += deltaTime;
        const int32 prevFrame = _currentFrame;
        // 프레임마다 시간이 다르다(클립의 durationMs). 한 틱에 여러 프레임을 넘길 수 있다 — 느린 프레임에서 시간이 밀리지 않게.
        while ( _frameTimer >= getFrameDuration( _currentFrame ) )
        {
            _frameTimer -= getFrameDuration( _currentFrame );
            ++_currentFrame;

            if ( _currentFrame >= _totalFrames )
            {
                if ( _bRepeat == SW_TRUE )
                {
                    _currentFrame = 0;
                }
                else if ( tryAdvanceGraphNode() )
                {
                    return;
                }
                else
                {
                    _currentFrame = _totalFrames - 1;
                    _bPlaying     = SW_FALSE;
                    break;
                }
            }
        }

        if ( _currentFrame != prevFrame )
            updateSpriteFrame();
        // 키는 프레임 사이에서도 보간된다 — 프레임이 그대로여도 매 틱 다시 읽는다.
        applyTransformKeys();
    }

    void SpriteAnimatorComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        static const hashed_string s_graphPathName( "_animationGraphPath" );
        if ( propertyName != s_graphPathName )
            return;

        tryLoadAnimationGraph();
        if ( _bPlaying == SW_FALSE || _bGraphLoaded == SW_FALSE )
            return;
        // 지금 애니메이션이 새 그래프에도 있으면 그대로 잇는다. 없으면 그 이름은 이제 "끝나면 다음" 을 찾지 못한다 — 새 목록의 처음으로.
        if ( _graph.findNodeByName( _currentAnimation ) == nullptr && _listAnimation.empty() == false )
            play( _listAnimation[0], _bRepeat == SW_TRUE );
    }

    void SpriteAnimatorComponent::play( const string& animName )
    {
        _currentAnimation = animName;
        _bPlaying         = SW_TRUE;
        _bPaused          = SW_FALSE;
        _currentFrame     = 0;
        _frameTimer       = 0.0f;
        _bRootWarned      = SW_FALSE;
        if ( _frameRate <= 0.0f )
            _frameRate = 12.0f;
        resolveFrameRange( true );
        updateSpriteFrame();
        applyTransformKeys();
    }

    void SpriteAnimatorComponent::play( const string& animName, bool loop )
    {
        play( animName );
        _bRepeat = loop ? SW_TRUE : SW_FALSE;
    }

    void SpriteAnimatorComponent::stop()
    {
        _bPlaying     = SW_FALSE;
        _bPaused      = SW_FALSE;
        _currentFrame = 0;
        _frameTimer   = 0.0f;
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
        _frameTimer   = 0.0f;
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
    }

    float32 SpriteAnimatorComponent::getFrameRate() const
    {
        return _frameRate;
    }

    void SpriteAnimatorComponent::setFrameRate( float32 rate )
    {
        _frameRate = rate;
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

    void SpriteAnimatorComponent::tryLoadAnimationGraph()
    {
        _bGraphLoaded = SW_FALSE;
        _graph        = AnimationGraphAsset{};
        if ( _animationGraphPath.empty() )
            return;
        if ( _graph.loadFromFile( _animationGraphPath ) == false )
            return;
        _bGraphLoaded = SW_TRUE;
        _graph.collectNodeNames( _listAnimation );
    }

    bool SpriteAnimatorComponent::tryAdvanceGraphNode()
    {
        if ( _bGraphLoaded == SW_FALSE )
            return false;
        const AnimationGraphNode* pNode = _graph.findNodeByName( _currentAnimation );
        if ( pNode == nullptr )
            return false;
        const int32               nextId = _graph.findFirstOutgoingNodeId( pNode->_id );
        const AnimationGraphNode* pNext  = _graph.findNode( nextId );
        if ( pNext == nullptr || pNext->_name.empty() )
            return false;
        play( pNext->_name, false );
        return true;
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

    void SpriteAnimatorComponent::resolveFrameRange( bool bTakeLoopFromClip )
    {
        const SpriteClipAsset* pClip = findClip();
        _pRangeClip                  = pClip;
        _firstClipFrame              = 0;
        _totalFrames                 = 1;
        if ( pClip == nullptr )
            return;

        SpriteClipAnimation range{};
        if ( pClip->findFrameRange( _currentAnimation, range ) == false )
        {
            SW_LOG_WARNING( "Sprite clip has no animation '%#' - showing frame 0", _currentAnimation );
            return;
        }
        _firstClipFrame = range._firstFrame;
        _totalFrames    = MathUtil::max( range._frameCount, 1 );
        _currentFrame   = MathUtil::clamp( _currentFrame, 0, _totalFrames - 1 );
        if ( bTakeLoopFromClip )
            _bRepeat = ( range._bLoop == SW_TRUE ) ? SW_TRUE : SW_FALSE;
    }

    float32 SpriteAnimatorComponent::getFrameDuration( int32 frameInRange ) const
    {
        const float32          fallbackSeconds = 1.0f / MathUtil::max( _frameRate, 1.0f );
        const SpriteClipAsset* pClip           = findClip();
        return ( pClip != nullptr ) ? pClip->getFrameDurationSeconds( _firstClipFrame + frameInRange, fallbackSeconds ) : fallbackSeconds;
    }

    void SpriteAnimatorComponent::updateSpriteFrame()
    {
        SpriteComponent* pSprite = findSprite();
        if ( pSprite == nullptr )
            return;
        pSprite->setClipFrame( _firstClipFrame + _currentFrame );
    }

    float32 SpriteAnimatorComponent::computeClipTime() const
    {
        const SpriteClipAsset* pClip = findClip();
        if ( pClip == nullptr )
            return 0.0f;
        const float32 fallbackSeconds = 1.0f / MathUtil::max( _frameRate, 1.0f );
        const int32   clipFrame       = _firstClipFrame + _currentFrame;
        // 반복하지 않는 구간이 끝나면 타이머에 남은 시간이 프레임 시간을 넘는다 — 구간 끝 시각에 멈춘다.
        const float32 timeInFrame = MathUtil::clamp( _frameTimer, 0.0f, getFrameDuration( _currentFrame ) );
        return pClip->computeFrameStartSeconds( clipFrame, fallbackSeconds ) + timeInFrame;
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
