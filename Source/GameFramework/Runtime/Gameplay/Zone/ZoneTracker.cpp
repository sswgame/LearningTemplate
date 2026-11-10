#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneTracker.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Reflection/ReflectionCast.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        struct ZoneTrackerInternal
        {
            /** @brief 대상의 태그입니다 — 오브젝트 태그(물체의 `Flammable` · `Fragile`)와 어빌리티가 준 태그(이펙트의 `State.Fireproof`)를 합친다. */
            static TagContainer collectTargetTags( const GameObject& target )
            {
                TagContainer tags = target.getTags();
                if ( const AbilityComponent* pAbility = target.getComponent<AbilityComponent>() )
                {
                    for ( const TagID tag : pAbility->getOwnedTagCounts().getTags().getTags() )
                    {
                        tags.addTag( tag );
                    }
                }
                return tags;
            }

            static float3 findPosition( const GameObject& object )
            {
                const SceneComponent* pScene = object.getPrimarySceneComponent();
                return pScene != nullptr ? pScene->getWorldPosition() : float3{};
            }

            /** @brief 대상을 쥔 AI 조종자입니다 — 조종자는 폰과 따로 자기 오브젝트에 산다(언리얼 AController). 같은 오브젝트에 있으면 그것이다. 없으면 nullptr 입니다. */
            static AIControllerComponent* findAIController( const GameObjectManager& manager, GameObject& target )
            {
                if ( AIControllerComponent* pSame = target.getComponent<AIControllerComponent>() )
                    return pSame;
                const PawnComponent* pPawn       = target.getComponent<PawnComponent>();
                Component*           pController = pPawn != nullptr ? manager.resolveComponent( pPawn->getController() ) : nullptr;
                return pController != nullptr ? castTo<AIControllerComponent>( pController ) : nullptr;
            }

            /** @brief 물리로 움직이는 개체(캐릭터 · 움직이는 강체)인지입니다 — 근처 역할이 고른다. */
            static bool isPhysicalBody( const GameObject& object )
            {
                const RigidBodyComponent* pBody = object.getComponent<RigidBodyComponent>();
                return object.getComponent<CharacterControllerComponent>() != nullptr || ( pBody != nullptr && pBody->getBodyType() == PhysicsBodyType::Dynamic );
            }
        };
    } // namespace

    ZoneTracker::ZoneTracker()
        : _listOccupancy{}
        , _arrLog{}
        , _logCount{ 0 }
        , _stamp{ 0 }
    {
    }

    void ZoneTracker::beginFrame()
    {
        ++_stamp;
    }

    void ZoneTracker::touch( const GameObject& target, const ZoneSample& sample )
    {
        const GameObjectHandle handle = target.getHandle();
        for ( OccupantRecord& occupancy : _listOccupancy )
        {
            if ( occupancy._target != handle )
                continue;
            const float32 timeInside      = occupancy._sample._timeInside;
            occupancy._sample             = sample;
            occupancy._sample._timeInside = timeInside; // 시간은 여기서 센다(입력은 모른다)
            occupancy._touchStamp         = _stamp;
            return;
        }
        OccupantRecord created;
        created._target             = handle;
        created._sample             = sample;
        created._sample._timeInside = 0.0f;
        created._touchStamp         = _stamp;
        _listOccupancy.push_back( std::move( created ) );
    }

    void ZoneTracker::dispatchOverlapEvents( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source, float32 deltaTime )
    {
        // 알리지 않은 대상은 나갔다. 사라진 대상(파괴 · 씬 전환)도 나간 것으로 친다 — 대상이 없으면 처리기는 부르지 않고 기록만 버린다.
        for ( size_t index = _listOccupancy.size(); index > 0; --index )
        {
            OccupantRecord& occupancy = _listOccupancy[index - 1];
            if ( occupancy._touchStamp == _stamp )
                continue;
            if ( occupancy._bEntered == SW_TRUE )
                dispatch( manager, profile, source, occupancy, Event::Exit, 0.0f );
            appendLog( source, occupancy, ZoneLogEvent::Exit );
            _listOccupancy.erase( _listOccupancy.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        // 처리기가 오브젝트를 만들고 지우면(재로 바뀜) 목록이 흔들리지 않게 차례로 다룬다 — 사건 안에서 이 목록은 바뀌지 않는다.
        for ( size_t index = 0; index < _listOccupancy.size(); ++index )
        {
            OccupantRecord& occupancy = _listOccupancy[index];
            if ( occupancy._bEntered == SW_FALSE )
            {
                occupancy._listMemory.assign( profile.getModules().size(), ZoneModuleMemory{} );
                occupancy._bEntered = SW_TRUE;
                appendLog( source, occupancy, ZoneLogEvent::Enter );
                dispatch( manager, profile, source, occupancy, Event::Enter, 0.0f );
            }
            occupancy._sample._timeInside += deltaTime;
            dispatch( manager, profile, source, occupancy, Event::Stay, deltaTime );
        }
    }

    void ZoneTracker::exitAllOccupants( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source )
    {
        ++_stamp; // 아무도 알리지 않은 프레임 — 모두 나간다
        dispatchOverlapEvents( manager, profile, source, 0.0f );
    }

    void ZoneTracker::onZoneOpened( GameObjectManager& manager, const ZoneCompiledProfile& profile )
    {
        for ( const ZoneCompiledModule& module : profile.getModules() )
        {
            module._pProcessor->onSourceBegin( manager, module );
        }
    }

    const ZoneTracker::OccupantRecord* ZoneTracker::findOccupancy( GameObjectHandle target ) const
    {
        for ( const OccupantRecord& occupancy : _listOccupancy )
        {
            if ( occupancy._target == target )
                return &occupancy;
        }
        return nullptr;
    }

    void ZoneTracker::collectEventLog( vector<ZoneLogEntry>& outListEntry ) const
    {
        const uint32 count = _logCount < kLogCapacity ? _logCount : kLogCapacity;
        for ( uint32 index = _logCount - count; index < _logCount; ++index )
        {
            outListEntry.push_back( _arrLog[index % kLogCapacity] );
        }
    }

    void ZoneTracker::appendLog( const ActionSource& source, const OccupantRecord& occupancy, ZoneLogEvent event )
    {
        ZoneLogEntry& entry = _arrLog[_logCount % kLogCapacity];
        entry._source       = source._name;
        entry._target       = occupancy._target;
        entry._timeInside   = occupancy._sample._timeInside;
        entry._event        = event;
        ++_logCount;
    }

    void ZoneTracker::dispatch( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source, OccupantRecord& occupancy, Event event, float32 deltaTime )
    {
        GameObject* const pOccupant = manager.resolveGameObject( occupancy._target );
        if ( pOccupant == nullptr || pOccupant->isPendingDestroy() )
            return;
        vector<SessionStateComponent*> listSession;
        SessionStateComponent::collectOpen( &manager, listSession );
        GameFlags* const pFlags = listSession.empty() ? nullptr : &listSession.front()->getFlags();

        const vector<ZoneCompiledModule>& listModule = profile.getModules();
        vector<GameObject*>               listTarget;
        for ( size_t moduleIndex = 0; moduleIndex < listModule.size() && moduleIndex < occupancy._listMemory.size(); ++moduleIndex )
        {
            const ZoneCompiledModule& module = listModule[moduleIndex];
            if ( pOccupant->isPendingDestroy() )
                return; // 앞 모듈의 할 일이 지웠다(재로 바뀜 · 파괴) — 남은 모듈은 할 대상이 없다
            listTarget.clear();
            resolveRoleTargets( manager, module, *pOccupant, listTarget );
            for ( GameObject* pTarget : listTarget )
            {
                ZoneSample sample                 = occupancy._sample;
                sample._pTarget                   = pTarget;
                TagContainer                 tags = ZoneTrackerInternal::collectTargetTags( *pTarget );
                ActionValues                 values;
                AIControllerComponent* const pAI = ZoneTrackerInternal::findAIController( manager, *pTarget );

                ZoneEventContext context;
                context._pModule   = &module;
                context._pMemory   = &occupancy._listMemory[moduleIndex];
                context._pTarget   = pTarget;
                context._pOccupant = pOccupant;
                context._pManager  = &manager;
                context._pSample   = &sample;
                context._world.set<ZoneSampleSlot>( &sample );
                context._world.set<SubjectTagsSlot>( &tags );
                context._world.set<ActionTargetSlot>( pTarget );
                context._world.set<ActionSourceSlot>( &source );
                context._world.set<GameFlagsSlot>( pFlags );
                context._world.set<GameFlagsWriteSlot>( pFlags );
                if ( pAI != nullptr )
                {
                    context._world.set<BlackboardSlot>( &pAI->getBlackboard() );
                    context._world.set<BlackboardWriteSlot>( &pAI->getBlackboard() );
                }
                module.evaluateActionParams( context._world, values );
                context._world.set<ActionValuesSlot>( &values );
                context._bConditionTrue = module._condition.evaluate( context._world ) ? SW_TRUE : SW_FALSE;
                switch ( event )
                {
                    case Event::Enter:
                        module._pProcessor->onEnter( context );
                        break;
                    case Event::Stay:
                        module._pProcessor->onStay( context, deltaTime );
                        break;
                    case Event::Exit:
                        module._pProcessor->onExit( context );
                        break;
                }
            }
        }
    }

    void ZoneTracker::resolveRoleTargets( GameObjectManager& manager, const ZoneCompiledModule& module, GameObject& occupant, vector<GameObject*>& outListTarget )
    {
        switch ( module._target )
        {
            case ZoneTargetRole::Inherit:
            case ZoneTargetRole::Occupant:
                outListTarget.push_back( &occupant );
                return;
            case ZoneTargetRole::Vehicle:
            {
                // 들어온 것이 탈것이면 그 자신, 탑승자면 그 좌석의 탈것이다.
                if ( occupant.getComponent<VehicleSeatComponent>() != nullptr )
                {
                    outListTarget.push_back( &occupant );
                    return;
                }
                const GameObjectHandle handle = occupant.getHandle();
                manager.forEachComponentOfType<VehicleSeatComponent>( [&]( VehicleSeatComponent* pSeat )
                {
                    GameObject* const pVehicle = pSeat->getOwner();
                    if ( pSeat->getOccupant() == handle && pVehicle != nullptr && outListTarget.empty() )
                        outListTarget.push_back( pVehicle );
                } );
                return;
            }
            case ZoneTargetRole::Riders:
            {
                occupant.forEachComponentOfType<VehicleSeatComponent>( [&]( VehicleSeatComponent* pSeat )
                {
                    GameObject* const pRider = pSeat->isFree() ? nullptr : manager.resolveGameObject( pSeat->getOccupant() );
                    if ( pRider != nullptr )
                        outListTarget.push_back( pRider );
                } );
                return;
            }
            case ZoneTargetRole::Nearby:
            {
                const ZoneCompiledParam* pRadius = module.findParam( hashed_string( "radius" ) );
                const float32            radius  = pRadius != nullptr ? module.evaluateNumber( pRadius->_name, GameplayContext{} ) : kDefaultNearbyRadius;
                const float3             center  = ZoneTrackerInternal::findPosition( occupant );
                // 근처는 물리로 움직이는 개체(캐릭터 · 강체)다 — 같은 씬의 컨트롤러와 강체를 훑는다.
                const auto consider = [&]( GameObject* pObject )
                {
                    if ( pObject == nullptr || pObject == &occupant || pObject->isPendingDestroy() || ZoneTrackerInternal::isPhysicalBody( *pObject ) == false )
                        return;
                    if ( ( ZoneTrackerInternal::findPosition( *pObject ) - center ).getLength() > radius )
                        return;
                    for ( const GameObject* pExisting : outListTarget )
                    {
                        if ( pExisting == pObject )
                            return;
                    }
                    outListTarget.push_back( pObject );
                };
                manager.forEachComponentOfType<CharacterControllerComponent>( [&]( CharacterControllerComponent* pController )
                { consider( pController->getOwner() ); } );
                manager.forEachComponentOfType<RigidBodyComponent>( [&]( RigidBodyComponent* pBody )
                { consider( pBody->getOwner() ); } );
                return;
            }
        }
    }
} // namespace sw
