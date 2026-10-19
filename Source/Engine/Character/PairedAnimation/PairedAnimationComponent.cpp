#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationComponent.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "PairedAnimation" );

    PairedAnimationComponent::PairedAnimationComponent()
        : _pairedAnimationPath{}
        , _role{}
        , _def{}
        , _sessionID{ 0 }
        , _seenReloadCount{ 0 }
        , _bSystemUser{ SW_FALSE }
    {
        // 세션은 씬의 프레임 시스템이 갱신한다. 자기 틱은 없다.
        setCanEverTick( false );
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
        _bSystemUser = SW_TRUE;
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
        _seenReloadCount                         = PairedAnimationDefCache::getReloadCount();
        shared_ptr<const PairedAnimationDef> def = PairedAnimationDefCache::acquire( _pairedAnimationPath );
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
        const bool bAssetReloaded = _pairedAnimationPath.empty() == false && _seenReloadCount != PairedAnimationDefCache::getReloadCount();
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
        uint32                           sessionID = 0;
        const PairedAnimationStartResult result    = PairedAnimationSystem::getOrCreate( *pManager ).startSession( *pManager, _def, listBinding, sessionID );
        if ( result == PairedAnimationStartResult::Started )
            _sessionID = sessionID;
        return result;
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
