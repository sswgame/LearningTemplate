#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneTracker.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Reflection/ReflectionCast.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneOccupantIndex.h"

#include <algorithm>

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
                if ( const AbilitySystemComponent* pAbility = target.getComponent<AbilitySystemComponent>() )
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
            static AIControllerComponent* findAIController( GameObjectManager& manager, GameObject& target )
            {
                if ( AIControllerComponent* pSame = target.getComponent<AIControllerComponent>() )
                    return pSame;
                const PawnComponent* pPawn       = target.getComponent<PawnComponent>();
                Component*           pController = pPawn != nullptr ? manager.resolveComponent( pPawn->getController() ) : nullptr;
                return pController != nullptr ? castTo<AIControllerComponent>( pController ) : nullptr;
            }
        };
    } // namespace

    ZoneTracker::ZoneTracker()
        : _listOccupancy{}
        , _listSpawnHeld{}
        , _listSpawnHeldNext{}
        , _spawnWindow{}
        , _arrLog{}
        , _logCount{ 0 }
        , _stamp{ 0 }
        , _spawnInside{ TriggerSpawnInside::Activate }
    {
    }

    void ZoneTracker::markAppeared( const GameObjectManager& manager )
    {
        _spawnWindow.markAppeared( manager );
        _listSpawnHeld.clear();
        _listSpawnHeldNext.clear();
    }

    bool ZoneTracker::holdSpawnedInside( const GameObject& target )
    {
        const GameObjectHandle   handle   = target.getHandle();
        const GameObjectManager* pManager = target.getManager();
        bool                     bHeld    = std::find( _listSpawnHeld.begin(), _listSpawnHeld.end(), handle ) != _listSpawnHeld.end();
        // 이미 대상인 것(걸어 들어와 안에 있다)은 빼지 않는다 — 새로 안에 든 것만 나타남인지 본다.
        bHeld = bHeld || ( findOccupancy( handle ) == nullptr && pManager != nullptr && _spawnWindow.isSampledSpawnEntry( *pManager, target ) ); // 지역은 프레임마다 재는 표본(touch)이다 — 물리 겹침 사건이 아니다
        if ( bHeld )
            _listSpawnHeldNext.push_back( handle );
        return bHeld;
    }

    void ZoneTracker::beginFrame()
    {
        ++_stamp;
    }

    void ZoneTracker::touch( const GameObject& target, const ZoneSample& sample )
    {
        if ( _spawnInside == TriggerSpawnInside::Ignore && holdSpawnedInside( target ) )
            return;
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
        // 나타남으로 빠진 대상 중 이번에 알리지 않은 것(나갔다)은 풀린다 — 다시 들어오면 진입이다.
        _listSpawnHeld.swap( _listSpawnHeldNext );
        _listSpawnHeldNext.clear();
        if ( _spawnInside == TriggerSpawnInside::Ignore )
            _spawnWindow.advance( manager );
        // 알리지 않은 대상은 나갔다. 사라진 대상(파괴 · 씬 전환)도 나간 것으로 친다 — 대상이 없으면 처리기는 부르지 않고 기록만 버린다.
        for ( size_t index = _listOccupancy.size(); index > 0; --index )
        {
            OccupantRecord& occupancy = _listOccupancy[index - 1];
            if ( occupancy._touchStamp == _stamp )
                continue;
            if ( occupancy._bEntered == SW_TRUE )
                updateModuleTargets( manager, profile, source, occupancy, false, 0.0f );
            appendLog( source, occupancy, ZoneLogEvent::Exit );
            _listOccupancy.erase( _listOccupancy.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        // 처리기가 오브젝트를 만들고 지우면(재로 바뀜) 목록이 흔들리지 않게 차례로 다룬다 — 사건 안에서 이 목록은 바뀌지 않는다.
        for ( size_t index = 0; index < _listOccupancy.size(); ++index )
        {
            OccupantRecord& occupancy = _listOccupancy[index];
            if ( occupancy._bEntered == SW_FALSE )
            {
                occupancy._bEntered = SW_TRUE;
                appendLog( source, occupancy, ZoneLogEvent::Enter );
            }
            updateModuleTargets( manager, profile, source, occupancy, true, deltaTime );
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
            if ( TriggerAuthorityRule::hasAuthority( module._pProcessor->getAuthority(), manager ) )
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

    void ZoneTracker::updateModuleTargets( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source, OccupantRecord& occupancy, bool bInside, float32 deltaTime )
    {
        const vector<ZoneCompiledModule>& listModule   = profile.getModules();
        const GameObjectHandle            occupant     = occupancy._target;
        const auto                        findOccupant = [&manager, occupant]() -> GameObject*
        {
            GameObject* const pOccupant = manager.resolveGameObject( occupant );
            return pOccupant != nullptr && pOccupant->isPendingDestroy() == false ? pOccupant : nullptr;
        };

        // 1. 대상이 바뀐 모듈의 이탈 · 진입
        vector<ModuleTarget>& listHeld = occupancy._listModuleTarget;
        vector<GameObject*>   listTarget;
        for ( size_t moduleIndex = 0; moduleIndex < listModule.size(); ++moduleIndex )
        {
            const ZoneCompiledModule& module = listModule[moduleIndex];
            if ( TriggerAuthorityRule::hasAuthority( module._pProcessor->getAuthority(), manager ) == false )
                continue; // 이 월드가 판정하지 않는 효과(클라이언트의 Server 모듈) — 대상 기록은 같이 두고 결과는 복제로 온다
            listTarget.clear();
            GameObject* const pOccupant = bInside ? findOccupant() : nullptr;
            if ( bInside && pOccupant == nullptr )
                return; // 앞 모듈의 할 일이 지웠다(재로 바뀜 · 파괴) — 남은 대상의 이탈은 다음 갱신이 부른다
            if ( pOccupant != nullptr )
                resolveRoleTargets( manager, module, *pOccupant, listTarget );
            for ( size_t heldIndex = listHeld.size(); heldIndex > 0; --heldIndex )
            {
                ModuleTarget held = listHeld[heldIndex - 1];
                if ( held._module != moduleIndex )
                    continue;
                GameObject* const pTarget = manager.resolveGameObject( held._target );
                if ( pTarget != nullptr && std::find( listTarget.begin(), listTarget.end(), pTarget ) != listTarget.end() )
                    continue;
                listHeld.erase( listHeld.begin() + static_cast<ptrdiff_t>( heldIndex - 1 ) );
                if ( pTarget != nullptr && pTarget->isPendingDestroy() == false )
                    dispatch( manager, module, source, occupancy, *pTarget, held._memory, Event::Exit, 0.0f );
            }
            for ( GameObject* pTarget : listTarget )
            {
                const GameObjectHandle target = pTarget->getHandle();
                const bool             bHeld  = std::any_of( listHeld.begin(), listHeld.end(), [moduleIndex, target]( const ModuleTarget& held )
                             { return held._module == moduleIndex && held._target == target; } );
                if ( bHeld )
                    continue;
                ModuleTarget entered;
                entered._target = target;
                entered._module = static_cast<uint32>( moduleIndex );
                listHeld.push_back( entered );
                dispatch( manager, module, source, occupancy, *pTarget, listHeld.back()._memory, Event::Enter, 0.0f );
            }
        }
        if ( bInside == false )
            return;

        // 2. 체류 — 진입을 모두 부른 뒤다(모듈의 조건이 같은 프레임에 다른 모듈이 건 것을 본다)
        occupancy._sample._timeInside += deltaTime;
        for ( size_t moduleIndex = 0; moduleIndex < listModule.size(); ++moduleIndex )
        {
            for ( size_t heldIndex = 0; heldIndex < listHeld.size(); ++heldIndex )
            {
                if ( findOccupant() == nullptr )
                    return; // 앞 모듈의 할 일이 지웠다 — 남은 모듈은 할 대상이 없다
                if ( listHeld[heldIndex]._module != moduleIndex )
                    continue;
                GameObject* const pTarget = manager.resolveGameObject( listHeld[heldIndex]._target );
                if ( pTarget != nullptr && pTarget->isPendingDestroy() == false )
                    dispatch( manager, listModule[moduleIndex], source, occupancy, *pTarget, listHeld[heldIndex]._memory, Event::Stay, deltaTime );
            }
        }
    }

    void ZoneTracker::dispatch( GameObjectManager& manager, const ZoneCompiledModule& module, const ActionSource& source, const OccupantRecord& occupancy, GameObject& target, ZoneModuleMemory& memory, Event event,
                                float32 deltaTime )
    {
        GameStateComponent* const pSession = GameStateComponent::findOpen( &manager );
        GameFlags* const          pFlags   = pSession != nullptr ? &pSession->getFlags() : nullptr;
        ZoneSample                sample   = occupancy._sample;
        sample._pTarget                    = &target;
        TagContainer                 tags  = ZoneTrackerInternal::collectTargetTags( target );
        ActionValues                 values;
        AIControllerComponent* const pAI = ZoneTrackerInternal::findAIController( manager, target );

        ZoneEventContext context;
        context._pModule   = &module;
        context._pMemory   = &memory;
        context._pTarget   = &target;
        context._pOccupant = manager.resolveGameObject( occupancy._target );
        context._pManager  = &manager;
        context._pSample   = &sample;
        context._world.set<ZoneSampleSlot>( &sample );
        context._world.set<SubjectTagsSlot>( &tags );
        context._world.set<ActionTargetSlot>( &target );
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
                for ( VehicleSeatComponent* pSeat : manager.getComponentRegistry().getAll<VehicleSeatComponent>() )
                {
                    GameObject* const pVehicle = pSeat->getOwner();
                    if ( pSeat->getOccupant() == handle && pVehicle != nullptr )
                    {
                        outListTarget.push_back( pVehicle );
                        break;
                    }
                }
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
                // 근처는 물리로 움직이는 개체(캐릭터 · 움직이는 강체)다 — 씬을 훑지 않고 씬 공간 색인의 물리 층에서 고른다(지역 볼륨 · 표면 지역이 그 층의 사용자다).
                vector<Component*>            listKey;
                vector<ZoneOccupantCandidate> listCandidate;
                ZoneOccupantIndex::queryCandidates( manager, radius * 2.0f, center._x - radius, center._z - radius, center._x + radius, center._z + radius, listKey, listCandidate );
                for ( const ZoneOccupantCandidate& candidate : listCandidate )
                {
                    GameObject* const pObject = candidate._pObject;
                    if ( pObject == &occupant || pObject->isPendingDestroy() || ( ZoneTrackerInternal::findPosition( *pObject ) - center ).getLength() > radius )
                        continue;
                    if ( std::find( outListTarget.begin(), outListTarget.end(), pObject ) == outListTarget.end() )
                        outListTarget.push_back( pObject );
                }
                return;
            }
        }
    }
} // namespace sw
