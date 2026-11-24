#include "pch.h"

#include "Games/Playground/FeatureDemo/SmartObjectDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

#include "Games/Playground/FeatureDemo/FeatureDemoCommandUtil.h"

namespace sw
{
    namespace
    {
        struct SmartObjectDemoComponentInternal
        {
            static constexpr uint32  kStateTag       = FourCCUtil::make( "FDSO" );
            static constexpr uint32  kStateVersion   = 1;
            static constexpr int32   kMinutesPerHour = 60;
            static constexpr float32 kMaxHour        = 24.0f; ///< 첫날의 끝

            /** @brief `schedule.advance <demo object> <hour>` — 첫날 그 시각(소수 시간)까지 일정을 흘립니다. */
            static bool runAdvance( const vector<string>& listArgument, string& outReply )
            {
                float32 hour = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], hour ) == false || hour < 0.0f || hour > kMaxHour )
                    return false;
                SmartObjectDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<SmartObjectDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->advanceToMinute( static_cast<int32>( hour * static_cast<float32>( kMinutesPerHour ) ) );
                FeatureDemoCommandUtil::reply( outReply, "schedule at hour %#", hour );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ScheduleAdvance, "schedule.advance", "schedule.advance <demo object> <hour>", "Advance the schedule of a smart object demo to an hour of day 0",
                    &SmartObjectDemoComponentInternal::runAdvance );

    SmartObjectDemoComponent::SmartObjectDemoComponent()
        : _scheduleDataPath{}
        , _startMinute{ 0 }
        , _minute{ 0 }
        , _claimCount{ 0 }
        , _refusedCount{ 0 }
        , _releaseCount{ 0 }
        , _pCatalog{}
        , _locator{}
        , _schedule{}
        , _listClaim{}
    {
    }

    SmartObjectDemoComponent::~SmartObjectDemoComponent() = default;

    bool SmartObjectDemoComponent::startGame()
    {
        if ( acquireRequired( _scheduleDataPath, _pCatalog ) == false )
            return false;
        const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        Scene* const              pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        if ( pScene == nullptr || pScene->getObjectManager() != getObjectManager() )
        {
            SW_LOG_ERROR_ID( "FeatureDemo.SmartObjectNotInActiveScene", "Smart object demo: it is not in the active scene - the reservations cannot name its objects" );
            return false;
        }
        _locator.initialize( pScene );
        _schedule.setActivityLocator( &_locator );
        if ( _schedule.initialize( _pCatalog.get(), ScheduleSystemSettings{}, _startMinute ) == false )
            return false; // 일정 시스템이 까닭을 남겼다
        _minute = _startMinute;
        _listClaim.assign( static_cast<size_t>( _schedule.getNPCCount() ), 0 );
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNPCCount(); ++npcIndex )
        {
            _schedule.setNPCLOD( npcIndex, ScheduleLOD::Near );
            if ( findBody( npcIndex ) == nullptr )
            {
                SW_LOG_ERROR_ID( "FeatureDemo.SmartObjectNoBody", "Smart object demo: the scene has no object named '%#' for its schedule NPC", _schedule.getNPCID( npcIndex ).c_str() );
                return false;
            }
        }
        return true;
    }

    GameObject* SmartObjectDemoComponent::findBody( int32 npcIndex )
    {
        return getObjectManager()->findGameObjectByName( _schedule.getNPCID( npcIndex ) );
    }

    void SmartObjectDemoComponent::advanceToMinute( int32 minute )
    {
        if ( minute <= _minute )
            return;
        _minute = minute;
        _schedule.advanceTo( minute );
        processEvents();
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNPCCount(); ++npcIndex )
        {
            GameObject*     pBody  = findBody( npcIndex );
            SceneComponent* pScene = pBody != nullptr ? pBody->getPrimarySceneComponent() : nullptr;
            if ( pScene != nullptr )
                pScene->setWorldPosition( getObjectManager()->getWorldOrigin().toLocal( _schedule.getNPCView( npcIndex )._location._position ) ); // 일정은 절대 자리로 산다
        }
    }

    void SmartObjectDemoComponent::processEvents()
    {
        vector<ScheduleEvent> listEvent;
        _schedule.pollEvents( listEvent );
        for ( const ScheduleEvent& event : listEvent )
        {
            if ( event._kind == ScheduleEvent::Kind::ActivityStarted )
                claimSlot( event._npcIndex );
            else if ( event._kind == ScheduleEvent::Kind::ActivityEnded )
                releaseSlot( event._npcIndex );
        }
    }

    uint32 SmartObjectDemoComponent::findReservation( int32 npcIndex ) const
    {
        const NPCScheduleView          view        = _schedule.getNPCView( npcIndex );
        const vector<ScheduleSegment>& listSegment = _schedule.getPlan( npcIndex );
        const bool                     bInPlan     = view._segmentIndex.has_value() && *view._segmentIndex < static_cast<int32>( listSegment.size() );
        return bInPlan ? listSegment[static_cast<size_t>( *view._segmentIndex )]._reservationID : 0;
    }

    void SmartObjectDemoComponent::claimSlot( int32 npcIndex )
    {
        const uint32      reservationID = findReservation( npcIndex );
        const GameObject* pBody         = findBody( npcIndex );
        if ( reservationID == 0 || pBody == nullptr )
            return; // 슬롯을 쓰지 않는 활동이거나 몸이 사라졌다
        if ( _locator.claimReserved( reservationID, *pBody ) == false )
        {
            ++_refusedCount;
            return;
        }
        ++_claimCount;
        _listClaim[static_cast<size_t>( npcIndex )] = reservationID;
    }

    void SmartObjectDemoComponent::releaseSlot( int32 npcIndex )
    {
        uint32& inoutClaim = _listClaim[static_cast<size_t>( npcIndex )];
        if ( inoutClaim == 0 )
            return;
        const GameObject* pBody = findBody( npcIndex );
        if ( pBody != nullptr && _locator.releaseClaim( inoutClaim, *pBody ) )
            ++_releaseCount;
        inoutClaim = 0;
    }

    void SmartObjectDemoComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime; // 시각은 명령으로만 흐른다
    }

    void SmartObjectDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)manager;
        (void)bRespawnViews;
    }

    void SmartObjectDemoComponent::serializeGameState( StateIO& io )
    {
        // 슬롯 점유는 스마트 오브젝트가, 시각 · 셈은 상태 칸이 싣는다. 일정은 시각의 함수라 그 시각으로 건너뛰고(이벤트는 버린다), 점유를 잇는 예약 번호만 다시 찾는다.
        io.header( SmartObjectDemoComponentInternal::kStateTag, SmartObjectDemoComponentInternal::kStateVersion );
        if ( io.isWriting() || io.isOk() == false )
            return;
        _schedule.skipTo( _minute );
        vector<ScheduleEvent> listSkipped;
        _schedule.pollEvents( listSkipped );
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNPCCount(); ++npcIndex )
        {
            const uint32      reservationID             = findReservation( npcIndex );
            const GameObject* pBody                     = findBody( npcIndex );
            const bool        bHeld                     = reservationID != 0 && pBody != nullptr && _locator.claimReserved( reservationID, *pBody ); // 이미 앉아 있으면 그대로 참이다
            _listClaim[static_cast<size_t>( npcIndex )] = bHeld ? reservationID : 0;
        }
    }
} // namespace sw
