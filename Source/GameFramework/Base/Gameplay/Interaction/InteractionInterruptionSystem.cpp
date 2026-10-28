#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionInterruptionSystem.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractorComponent.h"

namespace sw
{
    InteractionInterruptionSystem::InteractionInterruptionSystem()
        : _pSchedule{ nullptr }
        , _pResolver{ nullptr }
        , _interactionID{}
        , _interruptID{ "Talk" }
        , _listActive{}
    {
    }

    void InteractionInterruptionSystem::initialize( ScheduleSystem* pSchedule, const hashed_string& interactionID, const hashed_string& interruptID )
    {
        clear();
        _pSchedule     = pSchedule;
        _interactionID = interactionID;
        _interruptID   = interruptID;
    }

    void InteractionInterruptionSystem::clear()
    {
        if ( _pSchedule != nullptr )
        {
            for ( const Active& active : _listActive )
            {
                (void)_pSchedule->popInterruption( active._npcIndex, _interruptID ); // 이미 만료되어 빠졌으면 false 이고 할 일이 없다
            }
        }
        _listActive.clear();
    }

    int32 InteractionInterruptionSystem::resolveNpc( const GameObject& target ) const
    {
        if ( _pSchedule == nullptr )
            return -1;
        if ( _pResolver != nullptr )
            return _pResolver( *_pSchedule, target );
        return _pSchedule->findNpcIndex( target.getName() );
    }

    void InteractionInterruptionSystem::update( GameObjectManager& manager )
    {
        if ( _pSchedule == nullptr )
            return;
        vector<const GameObject*> listTarget;
        manager.forEachComponentOfType<InteractorComponent>( [&]( InteractorComponent* pInteractor )
        {
            if ( pInteractor->isInteracting() == false )
                return;
            const InteractionDef* pDef = pInteractor->getSession().getDefinition();
            if ( _interactionID.empty() == false && ( pDef == nullptr || pDef->_id != _interactionID ) )
                return;
            const GameObject* pTarget = manager.resolveGameObject( pInteractor->getFocus() );
            if ( pTarget != nullptr )
                listTarget.push_back( pTarget );
        } );
        applyTargets( listTarget );
    }

    void InteractionInterruptionSystem::applyTargets( const vector<const GameObject*>& listTarget )
    {
        if ( _pSchedule == nullptr )
            return;

        // 새로 말을 거는 대상은 쌓는다. 같은 대상을 여럿이 걸어도 한 번만이다.
        vector<Active> listNow;
        for ( const GameObject* pTarget : listTarget )
        {
            if ( pTarget == nullptr )
                continue;
            const uint64 targetID   = pTarget->getObjectID();
            bool         bDuplicate = false;
            for ( const Active& now : listNow )
            {
                bDuplicate = bDuplicate || now._targetObjectID == targetID;
            }
            if ( bDuplicate )
                continue;
            const int32 npcIndex = resolveNpc( *pTarget );
            if ( npcIndex < 0 )
                continue;
            Active now;
            now._targetObjectID = targetID;
            now._npcIndex       = npcIndex;
            listNow.push_back( now );
        }

        // 말하는 동안은 매 프레임 쌓는다 — 이미 쌓인 끼어들기는 만료 시각만 다시 세므로, 말이 `timeout` 보다 길어도 NPC 가 중간에 일정으로 돌아가지 않는다.
        for ( const Active& now : listNow )
        {
            (void)_pSchedule->pushInterruption( now._npcIndex, _interruptID ); // 모르는 id 는 일정이 경고한다. 쌓지 못한 대상도 추적하므로 아래에서 빼도 해롭지 않다
        }

        // 말이 끝난 대상은 뺀다. 일정은 지금 시각의 칸으로 돌아간다.
        for ( const Active& active : _listActive )
        {
            bool bStillTalking = false;
            for ( const Active& now : listNow )
            {
                bStillTalking = bStillTalking || now._targetObjectID == active._targetObjectID;
            }
            if ( bStillTalking == false )
                (void)_pSchedule->popInterruption( active._npcIndex, _interruptID ); // 시간 초과로 이미 빠졌으면 false 이고 할 일이 없다
        }
        _listActive = std::move( listNow );
    }
} // namespace sw
