#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "PairedAnimation" );
} // namespace sw

namespace sw
{
    PairedAnimationSystem::PairedAnimationSystem()
        : _listSession{}
        , _nextSessionID{ 1 }
        , _userCount{ 0 }
    {
    }

    PairedAnimationSystem* PairedAnimationSystem::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`getOrCreate`).
        return static_cast<PairedAnimationSystem*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    PairedAnimationSystem& PairedAnimationSystem::getOrCreate( GameObjectManager& manager )
    {
        if ( PairedAnimationSystem* pSystem = find( manager ) )
            return *pSystem;
        unique_ptr<PairedAnimationSystem> system  = make_unique<PairedAnimationSystem>();
        PairedAnimationSystem&            created = *system;
        const bool                        bAdded  = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( system ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았다
        (void)bAdded;
        return created;
    }

    bool PairedAnimationSystem::unregisterUser()
    {
        SW_ASSERT( _userCount > 0 );
        --_userCount;
        return _userCount == 0;
    }

    void PairedAnimationSystem::runBeforeTick( GameObjectManager& manager, float32 deltaTime )
    {
        // 지난 프레임에 끝난 세션은 그 프레임 동안 조회됐다. 이제 뺀다.
        size_t keptCount = 0;
        for ( size_t sessionIndex = 0; sessionIndex < _listSession.size(); ++sessionIndex )
        {
            if ( _listSession[sessionIndex]->isPlaying() == false )
                continue;
            if ( keptCount != sessionIndex )
                _listSession[keptCount] = std::move( _listSession[sessionIndex] );
            ++keptCount;
        }
        _listSession.resize( keptCount );
        for ( unique_ptr<PairedAnimationSession>& session : _listSession )
        {
            session->update( manager, deltaTime );
        }
    }

    PairedAnimationStartResult PairedAnimationSystem::startSession( GameObjectManager& manager, shared_ptr<const PairedAnimationDef> def, const vector<PairedAnimationBinding>& listBinding,
                                                                    uint32& outSessionID )
    {
        outSessionID = 0;
        SW_ASSERT( def != nullptr );
        for ( const PairedAnimationBinding& binding : listBinding )
        {
            if ( isParticipant( binding._animator ) )
                return PairedAnimationStartResult::ParticipantBusy;
        }
        unique_ptr<PairedAnimationSession> session = sw::make_unique<PairedAnimationSession>( _nextSessionID, std::move( def ) );
        const PairedAnimationStartResult   result  = session->start( manager, listBinding );
        if ( result != PairedAnimationStartResult::Started )
            return result;
        outSessionID = _nextSessionID;
        ++_nextSessionID;
        _listSession.push_back( std::move( session ) );
        return result;
    }

    bool PairedAnimationSystem::matchesSelect( GameObjectManager& manager, const PairedAnimationDef& def, const vector<PairedAnimationBinding>& listBinding )
    {
        const PairedAnimationSelectDef* pSelect = def.findSelect();
        if ( pSelect == nullptr )
            return true;
        const hashed_string&                        anchorRole = def.getRoles()[def.getAnchorRoleIndex()]._name;
        TComponentHandle<SkeletalAnimatorComponent> anchor{};
        TComponentHandle<SkeletalAnimatorComponent> selected{};
        for ( const PairedAnimationBinding& binding : listBinding )
        {
            if ( binding._role == anchorRole )
                anchor = binding._animator;
            else if ( binding._role == pSelect->_role )
                selected = binding._animator;
        }
        const SkeletalAnimatorComponent* pAnchor       = anchor.isValid() ? manager.resolveComponent( anchor ) : nullptr;
        const SkeletalAnimatorComponent* pSelected     = selected.isValid() ? manager.resolveComponent( selected ) : nullptr;
        const GameObject*                pAnchorOwner  = pAnchor != nullptr ? pAnchor->getOwner() : nullptr;
        const GameObject*                pSelectOwner  = pSelected != nullptr ? pSelected->getOwner() : nullptr;
        const SceneComponent*            pAnchorRoot   = pAnchorOwner != nullptr ? pAnchorOwner->getPrimarySceneComponent() : nullptr;
        const SceneComponent*            pSelectedRoot = pSelectOwner != nullptr ? pSelectOwner->getPrimarySceneComponent() : nullptr;
        if ( pAnchorRoot == nullptr || pSelectedRoot == nullptr )
            return false;
        return def.matchesSelect( pAnchorRoot->getWorldMatrix(), pSelectedRoot->getWorldPosition() );
    }

    PairedAnimationStartResult PairedAnimationSystem::startMatchingSession( GameObjectManager& manager, const vector<shared_ptr<const PairedAnimationDef>>& listDef,
                                                                            const vector<PairedAnimationBinding>& listBinding, uint32& outSessionID, uint32& outSetIndex )
    {
        outSessionID = 0;
        outSetIndex  = invalid_index::kUint32;
        for ( size_t setIndex = 0; setIndex < listDef.size(); ++setIndex )
        {
            SW_ASSERT( listDef[setIndex] != nullptr );
            if ( matchesSelect( manager, *listDef[setIndex], listBinding ) == false )
                continue;
            outSetIndex = static_cast<uint32>( setIndex );
            return startSession( manager, listDef[setIndex], listBinding, outSessionID );
        }
        return PairedAnimationStartResult::NoMatchingSet;
    }

    PairedAnimationStartResult PairedAnimationSystem::restoreSession( GameObjectManager& manager, shared_ptr<const PairedAnimationDef> def, const vector<PairedAnimationBinding>& listBinding,
                                                                      const PairedAnimationSessionSnapshot& snapshot )
    {
        SW_ASSERT( def != nullptr );
        if ( snapshot._sessionID == 0 )
        {
            SW_LOG_ERROR( "Paired animation snapshot has no session id" );
            return PairedAnimationStartResult::InvalidSnapshot;
        }
        size_t endedIndex = _listSession.size(); // 같은 id 로 끝나 아직 빠지지 않은 세션(없으면 끝 인덱스)
        for ( size_t sessionIndex = 0; sessionIndex < _listSession.size(); ++sessionIndex )
        {
            if ( _listSession[sessionIndex]->getID() != snapshot._sessionID )
                continue;
            if ( _listSession[sessionIndex]->isPlaying() )
                return PairedAnimationStartResult::SessionIDInUse;
            endedIndex = sessionIndex;
        }
        for ( const PairedAnimationBinding& binding : listBinding )
        {
            if ( isParticipant( binding._animator ) )
                return PairedAnimationStartResult::ParticipantBusy;
        }
        unique_ptr<PairedAnimationSession> session = sw::make_unique<PairedAnimationSession>( snapshot._sessionID, std::move( def ) );
        const PairedAnimationStartResult   result  = session->restore( manager, listBinding, snapshot );
        if ( result != PairedAnimationStartResult::Started )
            return result;
        if ( _nextSessionID <= snapshot._sessionID )
            _nextSessionID = snapshot._sessionID + 1;
        if ( endedIndex < _listSession.size() )
            _listSession[endedIndex] = std::move( session );
        else
            _listSession.push_back( std::move( session ) );
        return result;
    }

    void PairedAnimationSystem::abortSession( GameObjectManager& manager, uint32 sessionID )
    {
        for ( unique_ptr<PairedAnimationSession>& session : _listSession )
        {
            if ( session->getID() == sessionID )
                session->abort( manager );
        }
    }

    const PairedAnimationSession* PairedAnimationSystem::findSession( uint32 sessionID ) const
    {
        for ( const unique_ptr<PairedAnimationSession>& session : _listSession )
        {
            if ( session->getID() == sessionID )
                return session.get();
        }
        return nullptr;
    }

    bool PairedAnimationSystem::isParticipant( const TComponentHandle<SkeletalAnimatorComponent>& animator ) const
    {
        for ( const unique_ptr<PairedAnimationSession>& session : _listSession )
        {
            if ( session->isPlaying() && session->hasParticipant( animator ) )
                return true;
        }
        return false;
    }
} // namespace sw
