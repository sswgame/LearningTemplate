#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationComponent.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "PairedAnimation" );

    namespace
    {
        /** @brief `_bStartOnPlay` 의 시작이 참가자의 클립 로딩을 기다리는 최대 틱 수입니다(네트워크 복원의 `kMaxPendingFrames` 와 같다). */
        constexpr uint16 kMaxStartRetryFrames{ 300 };
    } // namespace

    PairedAnimationComponent::PairedAnimationComponent()
        : _pairedAnimationPath{}
        , _role{}
        , _def{}
        , _sessionID{ 0 }
        , _seenReloadCount{ 0 }
        , _startRetryFrames{ 0 }
        , _bSystemUser{ SW_FALSE }
        , _bStartOnPlay{ SW_FALSE }
        , _bStartPending{ SW_FALSE }
        , _reserved{ 0 }
    {
        // 세션은 씬의 프레임 시스템이 갱신한다. 틱은 `_bStartOnPlay` 의 첫 시작만 한다.
    }

    PairedAnimationComponent::~PairedAnimationComponent() = default;

    void PairedAnimationComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        loadDef();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        PairedAnimationSystem::getOrCreate( *pManager ).registerUser();
        _bSystemUser   = SW_TRUE;
        _bStartPending = _bStartOnPlay;
    }

    void PairedAnimationComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bStartPending == SW_FALSE )
            return;
        _bStartPending              = SW_FALSE;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 세션은 다른 오브젝트의 애니메이터를 바꾼다 — 틱 뒤에 시작한다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( PairedAnimationComponent& component, GameObjectManager& manager )
        {
            (void)manager;
            const PairedAnimationStartResult result = component.startSession( {} );
            if ( result == PairedAnimationStartResult::Started )
            {
                component._startRetryFrames = 0;
                return;
            }
            // 참가자의 클립을 아직 읽는 중이면 정상 거절이다 — 다음 틱에 다시 시도한다. 상한을 넘게 준비되지 않으면 그때 오류다.
            const bool bWaitingForClips = result == PairedAnimationStartResult::ParticipantNotReady && component._startRetryFrames < kMaxStartRetryFrames;
            if ( bWaitingForClips )
            {
                ++component._startRetryFrames;
                component._bStartPending = SW_TRUE;
                return;
            }
            SW_LOG_ERROR( "'%#': paired animation '%#' did not start on play (result %#, waited %# ticks)", component.getOwner()->getName().c_str(),
                          component._pairedAnimationPath.c_str(), static_cast<uint32>( result ), static_cast<uint32>( component._startRetryFrames ) );
        } );
    }

    void PairedAnimationComponent::onEndPlay()
    {
        abortSession();
        GameObject*            pOwner   = getOwner();
        GameObjectManager*     pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        PairedAnimationSystem* pSystem  = ( pManager != nullptr && _bSystemUser == SW_TRUE ) ? PairedAnimationSystem::find( *pManager ) : nullptr;
        _bSystemUser                    = SW_FALSE;
        // 마지막 사용자가 떠나면 시스템을 뗀다(모듈을 내리면 컴포넌트가 먼저 지워진다 — `ISceneFrameSystem` 의 약속).
        if ( pSystem != nullptr && pSystem->unregisterUser() )
            pManager->removeFrameSystem( hashed_string( PairedAnimationSystem::kFrameSystemKey ) );
        Component::onEndPlay();
    }

    void PairedAnimationComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_pathName( "_pairedAnimationPath" );
        if ( propertyName == s_pathName )
            loadDef();
    }

    void PairedAnimationComponent::setPairedAnimationPath( string_view path )
    {
        _pairedAnimationPath = string{ path };
        loadDef();
    }

    void PairedAnimationComponent::loadDef()
    {
        if ( _pairedAnimationPath.empty() )
            return;
        _seenReloadCount                         = SharedAssetCache<PairedAnimationDef>::getReloadCount();
        shared_ptr<const PairedAnimationDef> def = SharedAssetCache<PairedAnimationDef>::acquire( _pairedAnimationPath );
        if ( def == nullptr )
        {
            // 읽기 오류는 로더가 줄마다 남겼다. 지난 에셋으로 이어 가지 않는다.
            SW_LOG_ERROR( "'%#': paired animation '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _pairedAnimationPath.c_str() );
            _def.reset();
            return;
        }
        _def = std::move( def );
    }

    PairedAnimationStartResult PairedAnimationComponent::startSession( const vector<PairedAnimationBinding>& listOtherBinding )
    {
        GameObject*                pOwner    = getOwner();
        GameObjectManager*         pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        // 파일을 고쳐 캐시가 다시 읽었으면 새 에셋으로 시작한다(돌고 있는 세션은 옛 에셋을 끝까지 쥔다).
        const bool bAssetReloaded = _pairedAnimationPath.empty() == false && _seenReloadCount != SharedAssetCache<PairedAnimationDef>::getReloadCount();
        if ( bAssetReloaded )
            loadDef();
        if ( _def == nullptr || pManager == nullptr )
            return PairedAnimationStartResult::AssetNotLoaded;
        if ( pAnimator == nullptr )
        {
            SW_LOG_ERROR( "'%#': paired animation needs a SkeletalAnimatorComponent on its object", pOwner->getName().c_str() );
            return PairedAnimationStartResult::ParticipantMissing;
        }
        if ( getSessionState() == PairedAnimationSessionState::Playing )
            return PairedAnimationStartResult::ParticipantBusy;
        vector<PairedAnimationBinding> listBinding;
        listBinding.reserve( listOtherBinding.size() + 1 );
        listBinding.push_back( PairedAnimationBinding{ hashed_string( _role ), TComponentHandle<SkeletalAnimatorComponent>( pAnimator ) } );
        listBinding.insert( listBinding.end(), listOtherBinding.begin(), listOtherBinding.end() );
        if ( appendRoleComponentBindings( *pManager, listBinding ) == false )
            return PairedAnimationStartResult::ParticipantMissing;
        uint32                           sessionID = 0;
        const PairedAnimationStartResult result    = PairedAnimationSystem::getOrCreate( *pManager ).startSession( *pManager, _def, listBinding, sessionID );
        if ( result == PairedAnimationStartResult::Started )
            _sessionID = sessionID;
        return result;
    }

    bool PairedAnimationComponent::appendRoleComponentBindings( GameObjectManager& manager, vector<PairedAnimationBinding>& inoutListBinding ) const
    {
        vector<GameObject*> listObject;
        manager.getAllGameObjects( listObject );
        for ( const PairedAnimationRoleDef& role : _def->getRoles() )
        {
            const bool bBound = std::any_of( inoutListBinding.begin(), inoutListBinding.end(), [&role]( const PairedAnimationBinding& binding )
            { return binding._role == role._name; } );
            if ( bBound )
                continue;
            const SkeletalAnimatorComponent* pFound = nullptr;
            for ( GameObject* pObject : listObject )
            {
                const PairedAnimationComponent*  pOther     = pObject->getComponent<PairedAnimationComponent>();
                const bool                       bNamesRole = pOther != nullptr && pOther != this && pOther->_pairedAnimationPath == _pairedAnimationPath && hashed_string( pOther->_role ) == role._name;
                const SkeletalAnimatorComponent* pAnimator  = bNamesRole ? pObject->getComponent<SkeletalAnimatorComponent>() : nullptr;
                if ( pAnimator == nullptr )
                    continue;
                if ( pFound != nullptr )
                {
                    // 같은 역할을 적은 프롭이 둘이면 어느 쪽인지 고를 수 없다 — 조용히 첫째를 고르지 않는다.
                    SW_LOG_ERROR( "'%#': paired animation '%#' role '%#' is named by more than one object ('%#' and '%#')", getOwner()->getName().c_str(),
                                  _pairedAnimationPath.c_str(), role._name.c_str(), pFound->getOwner()->getName().c_str(), pObject->getName().c_str() );
                    return false;
                }
                pFound = pAnimator;
            }
            if ( pFound != nullptr )
                inoutListBinding.push_back( PairedAnimationBinding{ role._name, TComponentHandle<SkeletalAnimatorComponent>( pFound ) } );
        }
        return true;
    }

    void PairedAnimationComponent::abortSession()
    {
        GameObject*            pOwner   = getOwner();
        GameObjectManager*     pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        PairedAnimationSystem* pSystem  = pManager != nullptr ? PairedAnimationSystem::find( *pManager ) : nullptr;
        if ( pSystem != nullptr && _sessionID != 0 )
            pSystem->abortSession( *pManager, _sessionID );
    }

    PairedAnimationSessionState PairedAnimationComponent::getSessionState() const
    {
        const GameObject*             pOwner   = getOwner();
        const GameObjectManager*      pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const PairedAnimationSystem*  pSystem  = pManager != nullptr ? PairedAnimationSystem::find( *pManager ) : nullptr;
        const PairedAnimationSession* pSession = ( pSystem != nullptr && _sessionID != 0 ) ? pSystem->findSession( _sessionID ) : nullptr;
        return pSession != nullptr ? pSession->getState() : PairedAnimationSessionState::Idle;
    }
} // namespace sw
