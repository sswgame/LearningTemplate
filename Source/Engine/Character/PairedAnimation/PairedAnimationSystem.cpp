#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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
        unique_ptr<PairedAnimationSession> session = make_unique<PairedAnimationSession>( _nextSessionID, std::move( def ) );
        const PairedAnimationStartResult   result  = session->start( manager, listBinding );
        if ( result != PairedAnimationStartResult::Started )
            return result;
        outSessionID = _nextSessionID;
        ++_nextSessionID;
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

    bool PairedAnimationSystem::isParticipant( ComponentHandle animator ) const
    {
        for ( const unique_ptr<PairedAnimationSession>& session : _listSession )
        {
            if ( session->isPlaying() && session->hasParticipant( animator ) )
                return true;
        }
        return false;
    }
} // namespace sw
