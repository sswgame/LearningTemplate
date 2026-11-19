#include "pch.h"

#include "Games/Empty/KitDemo/KartRacingDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/KartRace.h"

namespace sw
{
    namespace
    {
        struct KartRacingDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDKR" );
            static constexpr uint32 kStateVersion = 2; ///< 2: 탄의 꿰뚫은 대상이 상한 없는 목록이다(탄 한 줄 길이가 바뀌었다)

            static constexpr const utf8* kTrackID        = "ring"; ///< 카탈로그의 한 바퀴 고리 트랙
            static constexpr int32       kRacerCount     = 3;
            static constexpr int32       kLeadRacer      = 2;     ///< 격자 앞(결승선 2 m 뒤)에 세워 먼저 달리게 하는 차
            static constexpr int32       kRunnerUpRacer  = 1;     ///< 결승선 3 m 뒤에 세워 0 번보다 앞서게 하는 차
            static constexpr float32     kLeadStart      = -2.0f; ///< 결승선 기준 거리(m)
            static constexpr float32     kRunnerUpStart  = -3.0f;
            static constexpr float32     kFinishLine     = 0.0f;
            static constexpr float32     kFinishTimeout  = 0.05f; ///< 1 등이 들어온 뒤 남은 차를 지금 순위로 끝내는 시한(초) — 몇 걸음 안에 끝난다
            static constexpr float32     kApproach       = 1.5f;  ///< 문 앞에서 차를 세우는 거리(m)
            static constexpr float32     kPassedGap      = 0.5f;  ///< 문을 이만큼 넘으면 지난 것으로 본다
            static constexpr int32       kMaxDriveSteps  = 240;   ///< 문 하나를 지나는 데 기다리는 걸음 상한
            static constexpr int32       kSettleSteps    = 10;    ///< 결승 뒤 시한이 지나도록 돌리는 걸음 수
            static constexpr int32       kFinishCrossing = 2;     ///< 격자에서 결승선을 넘으면 1 바퀴째, 한 번 더 넘으면 결승이다
            static constexpr int32       kNoRacer        = -1;

            static float32 computeTrackYaw( const KartTrack& track, float32 distance )
            {
                const KartTrackFrame frame = track.sample( distance );
                return MathUtil::atan2( frame._tangent._x, frame._tangent._z );
            }

            /** @brief 차를 거리 @p distance 의 문 앞에 세우고 페달을 밟아 넘을 때까지 달립니다. 넘지 못하면 false 입니다. */
            static bool driveThrough( KartRace& race, const KartTrack& track, int32 racer, float32 distance )
            {
                const float32 start = distance - kApproach;
                race.placeRacer( racer, track.sample( start )._position, computeTrackYaw( track, start ) );
                KartRacerInput input;
                input._vehicle._throttle = 1.0f;
                for ( int32 stepIndex = 0; stepIndex < kMaxDriveSteps; ++stepIndex )
                {
                    race.setInput( racer, input );
                    race.step();
                    if ( track.computeSignedGap( distance, race.findRacer( racer )->_distance ) > kPassedGap )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoKartRacingState, "KitDemo.KartRacing.State", "Kart racing demo state (0 not started, 1 race run finished)", KartRacingDemoComponent,
                                &KartRacingDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoKartRacingWinner, "KitDemo.KartRacing.Winner", "Racer index that finished first (-1 none)", KartRacingDemoComponent,
                                &KartRacingDemoComponent::getWinner );
    SW_AUTOMATION_GETTER_WATCH( kitDemoKartRacingRunnerUp, "KitDemo.KartRacing.RunnerUp", "Racer index placed second (-1 none)", KartRacingDemoComponent,
                                &KartRacingDemoComponent::getRunnerUp );
    SW_AUTOMATION_GETTER_WATCH( kitDemoKartRacingLast, "KitDemo.KartRacing.Last", "Racer index placed third (-1 none)", KartRacingDemoComponent, &KartRacingDemoComponent::getLast );

    KartRacingDemoComponent::KartRacingDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _winner{ KartRacingDemoComponentInternal::kNoRacer }
        , _runnerUp{ KartRacingDemoComponentInternal::kNoRacer }
        , _last{ KartRacingDemoComponentInternal::kNoRacer }
    {
    }

    KartRacingDemoComponent::~KartRacingDemoComponent() = default;

    bool KartRacingDemoComponent::runRace()
    {
        const KartTrackDef* pDef = _pCatalog->find( hashed_string( KartRacingDemoComponentInternal::kTrackID ) );
        KartTrack           track;
        if ( pDef == nullptr || track.initialize( *pDef ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.KartRacingOpenFailed", "Kart racing demo: the catalog has no usable track '%#' - the race does not open", KartRacingDemoComponentInternal::kTrackID );
            return false;
        }
        // 카운트다운 · 러버밴딩 없이 바로 달린다. 아이템 카탈로그가 없어 아이템도 없다.
        KartRaceSettings settings;
        settings._countdownTime = 0.0f;
        settings._finishTimeout = KartRacingDemoComponentInternal::kFinishTimeout;
        settings._bRubberBand   = SW_FALSE;
        KartRace race;
        if ( race.initialize( settings, &track, nullptr, nullptr ) == false )
            return false;
        for ( int32 index = 0; index < KartRacingDemoComponentInternal::kRacerCount; ++index )
        {
            if ( race.addRacer( KinematicVehicleSettings{}, false ).has_value() == false )
                return false;
        }
        race.start();
        race.placeRacer( KartRacingDemoComponentInternal::kLeadRacer, track.sample( KartRacingDemoComponentInternal::kLeadStart )._position,
                         KartRacingDemoComponentInternal::computeTrackYaw( track, KartRacingDemoComponentInternal::kLeadStart ) );
        race.step();
        race.placeRacer( KartRacingDemoComponentInternal::kRunnerUpRacer, track.sample( KartRacingDemoComponentInternal::kRunnerUpStart )._position,
                         KartRacingDemoComponentInternal::computeTrackYaw( track, KartRacingDemoComponentInternal::kRunnerUpStart ) );
        for ( int32 crossing = 0; crossing < KartRacingDemoComponentInternal::kFinishCrossing; ++crossing )
        {
            if ( KartRacingDemoComponentInternal::driveThrough( race, track, KartRacingDemoComponentInternal::kLeadRacer, KartRacingDemoComponentInternal::kFinishLine ) == false )
            {
                SW_LOG_ERROR_ID( "KitDemo.KartRacingStuck", "Kart racing demo: racer %# did not cross the finish line", KartRacingDemoComponentInternal::kLeadRacer );
                return false;
            }
        }
        for ( int32 stepIndex = 0; stepIndex < KartRacingDemoComponentInternal::kSettleSteps; ++stepIndex )
        {
            race.step();
        }
        // 확정 순위(1 부터)로 차 번호를 찾는다. 시한이 지나지 않아 정해지지 않은 순위는 -1 로 남는다.
        for ( int32 index = 0; index < race.getRacerCount(); ++index )
        {
            const int32 place = race.findRacer( index )->_finishPlace;
            if ( place == 1 )
                _winner = index;
            else if ( place == 2 )
                _runnerUp = index;
            else if ( place == 3 )
                _last = index;
        }
        markRunFinished();
        return true;
    }

    bool KartRacingDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runRace();
    }

    void KartRacingDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.KartRacingDone", "[KitDemo] kart racing: racer %# won, %# second, %# third", _winner, _runnerUp, _last );
    }

    void KartRacingDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, KartRacingDemoComponentInternal::kStateTag, KartRacingDemoComponentInternal::kStateVersion );
    }
} // namespace sw
