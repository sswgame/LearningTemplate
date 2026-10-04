#include "pch.h"

#include "Engine/Character/AnimNotifyComponent.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/CharacterHit.h"
#include "Engine/Character/SocketSetComponent.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "AnimNotify" );

    AnimNotifyListenerBinding::AnimNotifyListenerBinding( AnimNotifyComponent& owner )
        : _owner{ owner }
    {
    }

    void AnimNotifyListenerBinding::onAnimNotifiesFired( const AnimNotifyFrame& frame )
    {
        if ( frame._bFromTick == SW_TRUE )
            _owner.deferFrame( frame );
        else
            _owner.processFrame( frame );
    }

    AnimNotifyComponent::AnimNotifyComponent()
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
        // 애니메이터가 부른다 — 자기 틱은 없다.
        setCanEverTick( false );
    }

    AnimNotifyComponent::~AnimNotifyComponent()
    {
        unbindFromAnimator();
    }

    void AnimNotifyComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        loadNotifyTable();
        bindToAnimator();
    }

    void AnimNotifyComponent::onEndPlay()
    {
        closeAllStates();
        unbindFromAnimator();
        Component::onEndPlay();
    }

    void AnimNotifyComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_pathName( "_notifyTablePath" );
        if ( propertyName == s_pathName )
            loadNotifyTable();
    }

    void AnimNotifyComponent::setNotifyTablePath( string_view path )
    {
        _notifyTablePath = string{ path };
        loadNotifyTable();
    }

    void AnimNotifyComponent::setNotifyTable( shared_ptr<const AnimNotifyTable> table )
    {
        closeAllStates();
        _table = std::move( table );
    }

    void AnimNotifyComponent::loadNotifyTable()
    {
        // 경로가 비면 코드가 넣은 표(`setNotifyTable`)를 그대로 둔다.
        _seenTableReloadCount = AnimNotifyTableCache::getReloadCount();
        if ( _notifyTablePath.empty() )
            return;
        closeAllStates();
        _table = AnimNotifyTableCache::acquire( _notifyTablePath );
        if ( _table == nullptr )
            SW_LOG_ERROR( "'%#': notify table '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _notifyTablePath.c_str() );
    }

    void AnimNotifyComponent::bindToAnimator()
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
        SW_LOG_WARNING( "'%#': anim notify has no animator on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
    }

    void AnimNotifyComponent::unbindFromAnimator()
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

    const AnimNotifyEntry* AnimNotifyComponent::findEntry( const hashed_string& notify ) const
    {
        return _table != nullptr ? _table->findEntry( notify ) : nullptr;
    }

    void AnimNotifyComponent::deferFrame( const AnimNotifyFrame& frame )
    {
        // 워커(스프라이트 틱) — 베껴 두고 틱 뒤 게임 스레드에서 처리한다. 같은 오브젝트의 틱은 한 워커가 돌아 이 칸을 다른 워커가 만지지 않는다.
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
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            AnimNotifyComponent* pComponent = castTo<AnimNotifyComponent>( pManager->resolveComponent( self ) );
            if ( pComponent != nullptr )
                pComponent->processPendingFrame();
        } );
    }

    void AnimNotifyComponent::processPendingFrame()
    {
        if ( _bPendingFrame == SW_FALSE )
            return;
        _bPendingFrame = SW_FALSE;
        AnimNotifyFrame frame{};
        frame._listFired          = vector_reference<const AnimFiredNotify>{ _listPendingFired.data(), _listPendingFired.size() };
        frame._listActivePlayable = vector_reference<const IAnimPlayable* const>{ _listPendingActive.data(), _listPendingActive.size() };
        frame._deltaSeconds       = _pendingDeltaSeconds;
        frame._bRestarted         = _bPendingRestarted;
        _bPendingRestarted        = SW_FALSE;
        processFrame( frame );
    }

    void AnimNotifyComponent::callHandler( const AnimNotifyEntry& entry, const AnimFiredNotify& fired, AnimNotifyStateData* pState, AnimNotifyPhase phase, bool bTick,
                                           float32 deltaSeconds )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || entry._pHandler == nullptr )
            return;
        AnimNotifyContext context{ *this, *pOwner, entry, fired, pState, deltaSeconds };
        if ( bTick )
        {
            entry._pHandler->onNotifyTick( context );
            return;
        }
        switch ( phase )
        {
            case AnimNotifyPhase::Instant:
            {
                entry._pHandler->onNotify( context );
                break;
            }
            case AnimNotifyPhase::Begin:
            {
                entry._pHandler->onNotifyBegin( context );
                break;
            }
            case AnimNotifyPhase::End:
            {
                entry._pHandler->onNotifyEnd( context );
                break;
            }
        }
    }

    void AnimNotifyComponent::endState( size_t stateIndex, const AnimFiredNotify* pEndFired, float32 deltaSeconds )
    {
        // 처리기가 컴포넌트를 바꿀 수 있으므로 꺼내 놓고 부른다.
        ActiveState state = std::move( _listActiveState[stateIndex] );
        _listActiveState.erase( _listActiveState.begin() + static_cast<ptrdiff_t>( stateIndex ) );
        AnimFiredNotify fired = pEndFired != nullptr ? *pEndFired : state._fired;
        fired._phase          = AnimNotifyPhase::End;
        callHandler( state._entry, fired, &state._data, AnimNotifyPhase::End, false, deltaSeconds );
    }

    void AnimNotifyComponent::closeAllStates()
    {
        while ( _listActiveState.empty() == false )
            endState( _listActiveState.size() - 1, nullptr, 0.0f );
    }

    void AnimNotifyComponent::processFrame( const AnimNotifyFrame& frame )
    {
        _listAction.clear();
        // 표 파일을 고쳤으면 — 열린 구간을 옛 줄(베낀 것)로 닫고 새 표로 잇는다.
        if ( _notifyTablePath.empty() == false && _seenTableReloadCount != AnimNotifyTableCache::getReloadCount() )
            loadNotifyTable();

        // 0) 재생할 것이 바뀌었다(스프라이트가 같은 객체를 다른 구간으로) — 옛 구간의 열린 알림은 모두 끊긴 것이다.
        if ( frame._bRestarted == SW_TRUE )
            closeAllStates();
        // 1) 클립이 재생에서 빠진 구간은 끊긴 것이다 — 끝을 대신 낸다.
        for ( size_t stateIndex = _listActiveState.size(); stateIndex > 0; --stateIndex )
        {
            const ActiveState& state   = _listActiveState[stateIndex - 1];
            bool               bActive = false;
            for ( const IAnimPlayable* pPlayable : frame._listActivePlayable )
                bActive = bActive || pPlayable == state._pSource;
            if ( bActive == false )
                endState( stateIndex - 1, nullptr, frame._deltaSeconds );
        }

        // 2) 지난 프레임부터 열린 구간의 틱 — 이번 프레임의 움직임(칼 궤적)을 끝보다 먼저 잰다.
        for ( size_t stateIndex = 0; stateIndex < _listActiveState.size(); ++stateIndex )
        {
            ActiveState& state = _listActiveState[stateIndex];
            state._data._elapsed += frame._deltaSeconds;
            callHandler( state._entry, state._fired, &state._data, AnimNotifyPhase::Begin, true, frame._deltaSeconds );
        }

        // 3) 이번 프레임의 알림을 시각 순서로.
        for ( const AnimFiredNotify& fired : frame._listFired )
        {
            const AnimNotifyEntry* pEntry = findEntry( fired._name );
            if ( pEntry == nullptr || pEntry->_pHandler == nullptr )
                continue;
            const bool bState = pEntry->_pHandler->supportsState();
            if ( fired._phase == AnimNotifyPhase::End )
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
            if ( bState == false || fired._phase == AnimNotifyPhase::Instant )
            {
                // 길이 없는 알림 — 또는 구간을 받지 않는 처리기에 온 구간 알림의 시작. 한 번 부른다.
                callHandler( *pEntry, fired, nullptr, AnimNotifyPhase::Instant, false, frame._deltaSeconds );
                continue;
            }
            // 같은 구간이 이미 열려 있으면(끝을 못 보고 다시 시작 — 시각 점프) 먼저 닫는다.
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
            callHandler( opened._entry, opened._fired, &opened._data, AnimNotifyPhase::Begin, false, frame._deltaSeconds );
        }
    }

    bool AnimNotifyComponent::is2D() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr && pOwner->getComponent<SkeletalMeshComponent>() == nullptr;
    }

    bool AnimNotifyComponent::findSocketWorldTransform( const hashed_string& socketName, float4x4& outWorldTransform ) const
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

    float3 AnimNotifyComponent::findSocketWorldPosition( const hashed_string& socketName ) const
    {
        float4x4 world;
        (void)findSocketWorldTransform( socketName, world );
        return world.getTranslation();
    }

    bool AnimNotifyComponent::castFromTo( const float3& from, const float3& to, float32 radius, uint32 layerMask, CharacterRayHit& outHit ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return false;
        const float3  delta    = to - from;
        const float32 distance = delta.getLength();
        if ( distance <= 1.0e-5f )
            return false;
        const uint64 ignoreId = pOwner->getObjectId();
        if ( is2D() )
            return CharacterHitUtil::circleCast2D( *pManager, from, delta, distance, radius, layerMask, ignoreId, outHit );
        return CharacterHitUtil::sphereCast3D( *pManager, from, delta, distance, radius, layerMask, ignoreId, outHit );
    }
} // namespace sw
