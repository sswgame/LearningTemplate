#include "pch.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationNotify" );

    AnimationNotifyListenerBinding::AnimationNotifyListenerBinding( AnimationNotifyComponent& owner )
        : _owner{ owner }
    {
    }

    void AnimationNotifyListenerBinding::onAnimationNotifiesFired( const AnimationNotifyFrame& frame )
    {
        if ( frame._bFromTick == SW_TRUE )
            _owner.deferFrame( frame );
        else
            _owner.processFrame( frame );
    }

    AnimationNotifyComponent::AnimationNotifyComponent()
        : _notifyTablePath{}
        , _binding{ *this }
        , _table{}
        , _listActiveState{}
        , _listAction{}
        , _listPendingFired{}
        , _listPendingActive{}
        , _animator{}
        , _pendingDeltaSeconds{ 0.0f }
        , _seenTableReloadCount{ 0 }
        , _bPendingFrame{ SW_FALSE }
        , _bPendingRestarted{ SW_FALSE }
    {
        // 애니메이터가 호출한다. 자기 틱은 없다.
        setCanEverTick( false );
    }

    AnimationNotifyComponent::~AnimationNotifyComponent()
    {
        unbindFromAnimator();
    }

    void AnimationNotifyComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        loadNotifyTable();
        bindToAnimator();
    }

    void AnimationNotifyComponent::onEndPlay()
    {
        closeAllStates();
        unbindFromAnimator();
        Component::onEndPlay();
    }

    void AnimationNotifyComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_pathName( "_notifyTablePath" );
        if ( propertyName == s_pathName )
            loadNotifyTable();
    }

    void AnimationNotifyComponent::setNotifyTablePath( string_view path )
    {
        _notifyTablePath = string{ path };
        loadNotifyTable();
    }

    void AnimationNotifyComponent::setNotifyTable( shared_ptr<const AnimationNotifyTable> table )
    {
        closeAllStates();
        _table = std::move( table );
    }

    void AnimationNotifyComponent::loadNotifyTable()
    {
        // 경로가 비면 코드가 넣은 표(`setNotifyTable`)를 그대로 둔다.
        _seenTableReloadCount = AnimationNotifyTableCache::getReloadCount();
        if ( _notifyTablePath.empty() )
            return;
        closeAllStates();
        _table = AnimationNotifyTableCache::acquire( _notifyTablePath );
        if ( _table == nullptr )
            SW_LOG_ERROR( "'%#': notify table '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _notifyTablePath.c_str() );
    }

    void AnimationNotifyComponent::bindToAnimator()
    {
        GameObject*                pOwner    = getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator != nullptr )
        {
            pAnimator->setNotifyListener( &_binding );
            _animator = pAnimator->getHandle();
            return;
        }
        SpriteAnimatorComponent* pSprite = pOwner != nullptr ? pOwner->getComponent<SpriteAnimatorComponent>() : nullptr;
        if ( pSprite != nullptr )
        {
            pSprite->setNotifyListener( &_binding );
            _animator = pSprite->getHandle();
            return;
        }
        SW_LOG_WARNING( "'%#': animation notify has no animator on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
    }

    void AnimationNotifyComponent::unbindFromAnimator()
    {
        const GameObject*  pOwner            = getOwner();
        GameObjectManager* pManager          = pOwner != nullptr ? pOwner->getManager() : nullptr;
        Component*         pTarget           = ( pManager != nullptr && _animator.isValid() ) ? pManager->resolveComponent( _animator ) : nullptr;
        _animator                            = ComponentHandle{};
        SkeletalAnimatorComponent* pSkeletal = castTo<SkeletalAnimatorComponent>( pTarget );
        if ( pSkeletal != nullptr && pSkeletal->getNotifyListener() == &_binding )
            pSkeletal->setNotifyListener( nullptr );
        SpriteAnimatorComponent* pSprite = castTo<SpriteAnimatorComponent>( pTarget );
        if ( pSprite != nullptr && pSprite->getNotifyListener() == &_binding )
            pSprite->setNotifyListener( nullptr );
    }

    const AnimationNotifyEntry* AnimationNotifyComponent::findEntry( const hashed_string& notify ) const
    {
        return _table != nullptr ? _table->findEntry( notify ) : nullptr;
    }

    void AnimationNotifyComponent::deferFrame( const AnimationNotifyFrame& frame )
    {
        // 워커(스프라이트 틱)에서는 복사해 두고 틱 뒤 게임 스레드에서 처리한다. 같은 오브젝트의 틱은 한 워커가 돌므로 이 슬롯을 다른 워커가 만지지 않는다.
        _listPendingFired.assign( frame._listFired.begin(), frame._listFired.end() );
        _listPendingActive.assign( frame._listActivePlayable.begin(), frame._listActivePlayable.end() );
        _pendingDeltaSeconds        = frame._deltaSeconds;
        _bPendingRestarted          = ( _bPendingRestarted == SW_TRUE || frame._bRestarted == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        const bool bScheduled       = _bPendingFrame == SW_TRUE;
        _bPendingFrame              = SW_TRUE;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || bScheduled )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( AnimationNotifyComponent& component )
        { component.processPendingFrame(); } );
    }

    void AnimationNotifyComponent::processPendingFrame()
    {
        if ( _bPendingFrame == SW_FALSE )
            return;
        _bPendingFrame = SW_FALSE;
        AnimationNotifyFrame frame{};
        frame._listFired          = vector_reference<const AnimationFiredNotify>{ _listPendingFired.data(), _listPendingFired.size() };
        frame._listActivePlayable = vector_reference<const IAnimationPlayable* const>{ _listPendingActive.data(), _listPendingActive.size() };
        frame._deltaSeconds       = _pendingDeltaSeconds;
        frame._bRestarted         = _bPendingRestarted;
        _bPendingRestarted        = SW_FALSE;
        processFrame( frame );
    }

    void AnimationNotifyComponent::callHandler( const AnimationNotifyEntry& entry, const AnimationFiredNotify& fired, AnimationNotifyStateData* pState, AnimationNotifyPhase phase, bool bTick,
                                                float32 deltaSeconds )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || entry._pHandler == nullptr )
            return;
        AnimationNotifyContext context{ *this, *pOwner, entry, fired, pState, deltaSeconds };
        if ( bTick )
        {
            entry._pHandler->onNotifyTick( context );
            return;
        }
        switch ( phase )
        {
            case AnimationNotifyPhase::Instant:
            {
                entry._pHandler->onNotify( context );
                break;
            }
            case AnimationNotifyPhase::Begin:
            {
                entry._pHandler->onNotifyBegin( context );
                break;
            }
            case AnimationNotifyPhase::End:
            {
                entry._pHandler->onNotifyEnd( context );
                break;
            }
        }
    }

    void AnimationNotifyComponent::endState( size_t stateIndex, const AnimationFiredNotify* pEndFired, float32 deltaSeconds )
    {
        // 처리기가 컴포넌트를 바꿀 수 있으므로 꺼내 놓고 부른다.
        ActiveState state = std::move( _listActiveState[stateIndex] );
        _listActiveState.erase( _listActiveState.begin() + static_cast<ptrdiff_t>( stateIndex ) );
        AnimationFiredNotify fired = pEndFired != nullptr ? *pEndFired : state._fired;
        fired._phase               = AnimationNotifyPhase::End;
        callHandler( state._entry, fired, &state._data, AnimationNotifyPhase::End, false, deltaSeconds );
    }

    void AnimationNotifyComponent::closeAllStates()
    {
        while ( _listActiveState.empty() == false )
        {
            endState( _listActiveState.size() - 1, nullptr, 0.0f );
        }
    }

    void AnimationNotifyComponent::processFrame( const AnimationNotifyFrame& frame )
    {
        _listAction.clear();
        // 테이블 파일을 고쳤으면 열린 구간을 이전 항목(복사해 둔 것)으로 닫고 새 테이블로 이어 간다.
        if ( _notifyTablePath.empty() == false && _seenTableReloadCount != AnimationNotifyTableCache::getReloadCount() )
            loadNotifyTable();

        // 0) 재생할 것이 바뀌었다(스프라이트가 같은 객체를 다른 구간으로 재생한다). 이전 구간의 열린 알림은 모두 끊긴 것이다.
        if ( frame._bRestarted == SW_TRUE )
            closeAllStates();
        // 1) 클립이 재생에서 빠진 구간은 끊긴 것이다. 끝을 대신 낸다.
        for ( size_t stateIndex = _listActiveState.size(); stateIndex > 0; --stateIndex )
        {
            const ActiveState& state   = _listActiveState[stateIndex - 1];
            bool               bActive = false;
            for ( const IAnimationPlayable* pPlayable : frame._listActivePlayable )
            {
                bActive = bActive || pPlayable == state._pSource;
            }
            if ( bActive == false )
                endState( stateIndex - 1, nullptr, frame._deltaSeconds );
        }

        // 2) 지난 프레임부터 열린 구간의 틱이다. 이번 프레임의 움직임(칼 궤적)을 끝보다 먼저 측정한다.
        for ( size_t stateIndex = 0; stateIndex < _listActiveState.size(); ++stateIndex )
        {
            ActiveState& state = _listActiveState[stateIndex];
            state._data._elapsed += frame._deltaSeconds;
            callHandler( state._entry, state._fired, &state._data, AnimationNotifyPhase::Begin, true, frame._deltaSeconds );
        }

        // 3) 이번 프레임의 알림을 시각 순서로.
        for ( const AnimationFiredNotify& fired : frame._listFired )
        {
            const AnimationNotifyEntry* pEntry = findEntry( fired._name );
            if ( pEntry == nullptr || pEntry->_pHandler == nullptr )
                continue;
            const bool bState = pEntry->_pHandler->supportsState();
            if ( fired._phase == AnimationNotifyPhase::End )
            {
                for ( size_t stateIndex = 0; stateIndex < _listActiveState.size(); ++stateIndex )
                {
                    const ActiveState& state = _listActiveState[stateIndex];
                    if ( state._pSource == fired._pSource && state._eventIndex == fired._eventIndex )
                    {
                        endState( stateIndex, &fired, frame._deltaSeconds );
                        break;
                    }
                }
                continue;
            }
            if ( bState == false || fired._phase == AnimationNotifyPhase::Instant )
            {
                // 길이 없는 알림이거나, 구간을 받지 않는 처리기에 온 구간 알림의 시작이다. 한 번 호출한다.
                callHandler( *pEntry, fired, nullptr, AnimationNotifyPhase::Instant, false, frame._deltaSeconds );
                continue;
            }
            // 같은 구간이 이미 열려 있으면(끝을 못 보고 다시 시작한 경우, 즉 시각이 점프한 경우) 먼저 닫는다.
            for ( size_t stateIndex = 0; stateIndex < _listActiveState.size(); ++stateIndex )
            {
                if ( _listActiveState[stateIndex]._pSource == fired._pSource && _listActiveState[stateIndex]._eventIndex == fired._eventIndex )
                {
                    endState( stateIndex, nullptr, frame._deltaSeconds );
                    break;
                }
            }
            ActiveState state{};
            state._entry      = *pEntry;
            state._fired      = fired;
            state._pSource    = fired._pSource;
            state._eventIndex = fired._eventIndex;
            _listActiveState.push_back( std::move( state ) );
            ActiveState& opened = _listActiveState.back();
            callHandler( opened._entry, opened._fired, &opened._data, AnimationNotifyPhase::Begin, false, frame._deltaSeconds );
        }
    }

    bool AnimationNotifyComponent::is2D() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr && pOwner->getComponent<SkeletalMeshComponent>() == nullptr;
    }

    bool AnimationNotifyComponent::findSocketWorldTransform( const hashed_string& socketName, float4x4& outWorldTransform ) const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return false;
        if ( SocketLookupUtil::findSocketWorldTransform( *pOwner, socketName, outWorldTransform ) )
            return true;
        const SceneComponent* pRoot = pOwner->getPrimarySceneComponent();
        outWorldTransform           = pRoot != nullptr ? pRoot->getWorldMatrix() : float4x4::Identity;
        return false;
    }

    float3 AnimationNotifyComponent::findSocketWorldPosition( const hashed_string& socketName ) const
    {
        float4x4 world;
        (void)findSocketWorldTransform( socketName, world );
        return world.getTranslation();
    }

    bool AnimationNotifyComponent::castFromTo( const float3& from, const float3& to, float32 radius, uint32 layerMask, CharacterRayHit& outHit ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return false;
        const float3  delta    = to - from;
        const float32 distance = delta.getLength();
        if ( distance <= 1.0e-5f )
            return false;
        const uint64 ignoreID = pOwner->getObjectID();
        if ( is2D() )
            return CharacterHitUtil::circleCast2D( *pManager, from, delta, distance, radius, layerMask, ignoreID, outHit );
        return CharacterHitUtil::sphereCast3D( *pManager, from, delta, distance, radius, layerMask, ignoreID, outHit );
    }
} // namespace sw
