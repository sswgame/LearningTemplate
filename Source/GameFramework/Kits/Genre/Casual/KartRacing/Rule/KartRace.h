/**
 * @file KartRace.h
 * @brief 카트 경기 한 판의 규칙을 구현합니다.
 * @details 출발 격자와 카운트다운을 처리하고, 체크포인트를 순서대로 지나야 한 바퀴로 인정한다(지름길 방지). 바퀴 수와 중심선 진행 거리로 실시간 순위를 구하고,
 *          역주행을 감지하며, 랩 타임과 최고 기록을 남기고, 결승과 순위를 확정한다. 아이템 상자, 순위 가중 뽑기, 아이템 효과(바나나, 껍질, 유도, 부스터, 방어막, 1등 공격),
 *          맞았을 때의 회전과 감속, AI 운전과 러버밴딩, 고스트 기록도 이 클래스가 다룬다.
 *          차는 기반 `ArcadeVehicleMotor` 이고 트랙이 그 차의 땅이다. 경기는 고정 걸음(`FixedStepTimer`)으로 돌고 난수는 시드를 고정(`GameRandom`)하므로
 *          같은 입력이면 같은 경기가 나온다(리플레이, 락스텝). 차끼리의 충돌은 처리하지 않으며, 게임(물리)이 `ArcadeVehicleMotor::addImpulse` 로 넣는다.
 *
 *          한 바퀴: 결승선(문 0)을 앞으로 지날 때 그 바퀴의 체크포인트(문 1..)를 **모두 차례로** 지났으면 바퀴가 오른다. 체크포인트를 뒤로 지나면 그 통과가
 *          취소되고, 결승선을 뒤로 넘으면 바퀴가 내려간다. 그 바퀴 기록도 지우므로 앞뒤로 넘나들며 짧은 랩 타임을 만들 수 없다.
 *          출발 격자는 결승선 **뒤**에 있으므로 처음 결승선을 지나면 1바퀴째가 시작된다. `getLapCount()` 바퀴를 마치면 결승이다.
 *
 *          순위 진행값은 (바퀴 - 1) x 길이 + 중심선 거리이다. 거리는 지금 사이에 있는 두 문의 거리로 잘라, 문을 건너뛴 차가 앞선 것처럼 보이지 않게 한다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Movement/ArcadeVehicleMotor.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartAI.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartItems.h"

namespace sw
{
    class Archive;
    class KartGhost;
    class KartTrack;

    /** @brief 경기 설정입니다. */
    struct KartRaceSettings
    {
        float32 _step{ 1.0f / 60.0f };
        float32 _countdownTime{ 3.0f };
        float32 _finishTimeout{ 30.0f };    ///< 1등이 들어온 뒤 이 시간(초)이 지나면 남은 차를 지금 순위로 끝낸다
        float32 _gridFirstDistance{ 6.0f }; ///< 결승선 뒤 첫 줄까지의 거리(m)
        float32 _gridRowSpacing{ 6.0f };    ///< 줄 간격(m)이다. 한 줄에 두 대가 선다
        float32 _wrongWayDelay{ 1.0f };     ///< 이 시간(초) 동안 거꾸로 달리면 역주행으로 본다
        float32 _wrongWaySpeed{ 2.0f };     ///< 트랙 방향 속도가 이보다 크게 음수이면 거꾸로 달리는 것이다
        float32 _kartRadius{ 1.0f };        ///< 아이템과 상자에 맞는 차의 반지름
        float32 _itemBoxRadius{ 1.5f };
        float32 _itemBoxRespawn{ 2.0f };
        float32 _ownerGrace{ 0.5f };             ///< 쏜 차는 이 시간(초) 동안 자기 껍질에 맞지 않는다
        float32 _dropBehindDistance{ 2.5f };     ///< 바나나를 놓는 거리(차 뒤쪽)
        float32 _projectionRange{ 40.0f };       ///< 지난 거리 앞뒤로 이만큼 안에서만 중심선을 찾는다(트랙이 겹치는 경우를 위해서이다)
        float32 _rubberBandDistance{ 120.0f };   ///< 1등과 이만큼 벌어지면 보너스가 최대가 된다
        float32 _rubberBandMaxBonus{ 0.15f };    ///< AI 최고 속도 보너스이다(0.15 는 +15 %)
        float32 _rubberBandLeadPenalty{ 0.05f }; ///< 사람보다 앞선 AI 1등의 최고 속도 감소량
        float32 _redShellLockDistance{ 120.0f };
        float32 _redShellLockAngle{ 75.0f }; ///< 단위는 도(degree)이다
        uint32  _seed{ GameRandom::kDefaultSeed };
        uint8   _bRubberBand{ SW_TRUE };
        uint8   _bItems{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 경기 단계입니다. */
    enum class KartRacePhase : uint8
    {
        Setup = 0, ///< `start` 전이며 이때 차를 추가한다
        Countdown, ///< 차가 서 있다
        Racing,
        Ended
    };

    /** @brief 사람 차가 한 걸음에 받는 입력입니다. 눌림(`_bBoostPressed`, `_bJumpPressed`, `_bUseItem`)은 다음 걸음 하나에만 반영합니다. */
    struct KartRacerInput
    {
        ArcadeVehicleInput _vehicle{};
        uint8              _bUseItem{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 차 한 대의 경기 상태입니다. */
    struct KartRacer
    {
        ArcadeVehicleMotor    _motor{};
        KartAIDriver          _ai{};
        ArcadeVehicleSettings _baseSettings{};
        KartRacerInput        _input{};
        vector<float32>       _listLapTime{};
        hashed_string         _itemID{}; ///< 들고 있는 아이템이다(비어 있으면 없다)
        float3                _previousPosition{};
        float32               _distance{ 0.0f }; ///< 중심선 거리
        float32               _progress{ 0.0f }; ///< 순위 진행값
        float32               _lapStartTime{ 0.0f };
        float32               _bestLapTime{ 0.0f }; ///< 0 이면 아직 없다
        float32               _finishTime{ 0.0f };  ///< 0 이면 결승 전이거나 들어오지 못한 것이다
        Countdown             _spinTime{};          ///< 남아 있으면 맞아서 도는 중이다(조작 불가)
        Countdown             _shieldTime{};
        float32               _wrongWayTime{ 0.0f };
        float32               _itemHeldTime{ 0.0f };
        float32               _speedScale{ 1.0f };  ///< 러버밴딩 배율
        int32                 _lap{ 0 };            ///< 지금 바퀴이다(0 은 출발선 전)
        int32                 _nextCheckpoint{ 0 }; ///< 이번 바퀴에 지난 체크포인트 수
        int32                 _place{ 0 };
        int32                 _finishPlace{ 0 }; ///< 확정 순위이다(0 은 아직 정해지지 않음)
        int32                 _lastBoostPad{ -1 };
        uint8                 _bAI{ SW_FALSE };
        uint8                 _bFinished{ SW_FALSE };
        uint8                 _bWrongWay{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 날아가거나 놓인 아이템입니다. */
    struct KartProjectile
    {
        float3        _position{};
        float3        _direction{ 0.0f, 0.0f, 1.0f }; ///< 수평 단위 벡터
        hashed_string _itemID{};
        float32       _age{ 0.0f };
        int32         _owner{ -1 };
        int32         _target{ -1 }; ///< 유도 대상이다(-1 이면 곧게 나간다)
        KartItemKind  _kind{ KartItemKind::Banana };
        uint8         _bActive{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 경기에서 일어난 일을 알리는 이벤트입니다. */
    struct KartRaceEvent
    {
        enum class Kind : uint8
        {
            RaceStarted = 0,
            CheckpointPassed, ///< `_value` 는 지난 체크포인트 수이다
            CheckpointMissed, ///< 결승선을 지났지만 체크포인트가 모자라다. `_value` 는 지난 수이다
            LapStarted,       ///< `_value` 는 새 바퀴이다
            LapCompleted,     ///< `_value` 는 마친 바퀴이고 `_time` 은 랩 타임이다
            LapRevoked,       ///< 결승선을 뒤로 넘었다. `_value` 는 돌아간 바퀴이다
            FinalLap,
            NewBestLap,   ///< 경기 전체의 최고 기록이며 `_time` 을 쓴다
            Finished,     ///< `_value` 는 순위이고 `_time` 은 기록이다
            WrongWay,     ///< `_value` 가 1 이면 시작, 0 이면 끝이다
            PlaceChanged, ///< `_value` 는 새 순위이다
            ItemBoxTaken, ///< `_value` 는 상자 번호이고 `_itemID` 는 받은 아이템이다(비어 있으면 이미 들고 있었다)
            ItemUsed,
            Hit,           ///< `_racer` 는 맞은 차이고 `_other` 는 쏜 차이다
            ShieldBlocked, ///< `_racer` 는 막은 차이다
            BoostPad,
            RaceEnded
        };
        hashed_string _itemID{};
        float32       _time{ 0.0f };
        int32         _racer{ -1 };
        int32         _other{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::RaceStarted };
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartRace
     * @brief 한 경기를 진행합니다. 트랙과 아이템 카탈로그는 빌려 쓰므로 경기보다 오래 살아야 합니다.
     * @code
     *     race.initialize( settings, &track, &items );
     *     race.addRacer( kartSettings, false ); // 사람
     *     race.addRacer( kartSettings, true );  // AI
     *     race.start();
     *     // 매 프레임: race.setInput( 0, input ); race.update( deltaTime ); race.drainEvents( listEvent );
     * @endcode
     */
    class SW_GF_API KartRace
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "KRAC" );
        static constexpr uint32 kStateVersion = 1;

        KartRace();

        void initialize( const KartRaceSettings& settings, const KartTrack* pTrack, const KartItemCatalog* pItemCatalog );
        /** @brief 차를 추가하고 차 번호를 반환합니다(`Setup` 단계에서만 가능합니다). 실패하면 -1 입니다. 반환한 `KartRacer` 포인터는 다음 `addRacer` 까지만 유효합니다. */
        int32 addRacer( const ArcadeVehicleSettings& vehicleSettings, bool bAI );
        void  setRacerAI( int32 racer, const KartAISettings& settings );
        /** @brief 차를 출발 격자에 세우고 카운트다운을 시작합니다. 차가 없으면 아무것도 하지 않습니다. */
        void start();

        void setInput( int32 racer, const KartRacerInput& input );
        /** @brief 프레임 시간만큼 고정 걸음으로 진행하고, 진행한 걸음 수를 반환합니다. */
        int32 update( float32 frameTime );
        /** @brief 한 걸음 진행합니다. */
        void step();

        /**
         * @brief 차를 멈춰 세운 채 옮깁니다(코스 밖이나 낙사 구조, 테스트용). 옮겨도 문을 지난 것으로 치지 않습니다.
         * @details 드리프트는 보상 없이 끊고, 부스트와 니트로는 남긴다(`ArcadeVehicleMotor::reset` 은 니트로까지 비운다).
         */
        void placeRacer( int32 racer, const float3& position, float32 yaw );
        /** @brief 차 @p racer 의 입력을 @p pGhost 에 기록합니다(빌려 쓰는 포인터). 출발 위치에서 기록을 시작하고 결승에서 멈춥니다. */
        void startGhostRecording( int32 racer, KartGhost* pGhost );
        /** @brief 아이템을 쥐여 줍니다(테스트와 이벤트용). 들고 있던 아이템은 바뀝니다. */
        void giveItem( int32 racer, const hashed_string& itemID );
        /** @brief 들고 있는 아이템을 씁니다. 없으면 false 를 반환합니다. */
        [[nodiscard]] bool useItem( int32 racer );
        /** @brief 아이템에 맞습니다(게임 쪽 함정과 테스트용). 방어막이 막으면 false 를 반환합니다. */
        [[nodiscard]] bool applyHit( int32 victim, int32 attacker, const KartItemDef& def );
        /** @brief 러버밴딩 최고 속도 배율을 반환합니다(사람이거나 꺼져 있으면 1). */
        float32 computeRubberBandScale( int32 racer ) const;

        /**
         * @brief 차마다 차체, 드리프트 상태, 받아 둔 입력, 랩 기록, 아이템, 진행, 타이머, 순위를 쓰고, 이어서 투사체, 아이템 상자 타이머, 순위 순서, 난수, 고정 걸음, 시간, 단계를 씁니다.
         * @details 설정, 트랙, 아이템 카탈로그, 고스트(빌린 기록기)와 차마다의 기본 차 설정, AI 설정, 사람과 AI 의 구분(`addRacer` 와 `setRacerAI` 가 정한 값)은 저장하지 않는다.
         *          이벤트는 읽을 때 비운다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 차 수나 상자 수가 다르거나, 모르는 아이템 id 이거나, 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        KartRacePhase    getPhase() const { return _phase; }
        float32          getRaceTime() const { return _raceTime; }
        float32          getCountdown() const { return _countdown.getRemaining(); }
        float32          getBestLapTime() const { return _bestLapTime; }
        int32            getRacerCount() const { return static_cast<int32>( _listRacer.size() ); }
        const KartRacer* findRacer( int32 racer ) const;
        /** @brief @p place 등(1부터)인 차의 번호를 반환합니다. 없으면 -1 입니다. */
        int32                         findRacerAtPlace( int32 place ) const;
        const vector<KartProjectile>& getProjectiles() const { return _listProjectile; }
        bool                          isItemBoxActive( int32 boxIndex ) const;
        const KartTrack*              getTrack() const { return _pTrack; }
        void                          drainEvents( vector<KartRaceEvent>& outListEvent );

    private:
        bool               isValidRacer( int32 racer ) const { return 0 <= racer && racer < static_cast<int32>( _listRacer.size() ); }
        bool               isKnownItem( const hashed_string& itemID ) const; ///< 빈 id 이거나 카탈로그에 있는 아이템인지 반환한다(상태 읽기에서 쓴다)
        ArcadeVehicleInput resolveInput( int32 racer, bool& outUseItem );
        void               updateProgress( int32 racer );
        void               completeLap( int32 racer );
        void               finishRacer( int32 racer );
        float32            computeProgress( const KartRacer& kart ) const;
        void               updateItemBoxes( float32 deltaTime );
        void               updateProjectiles( float32 deltaTime );
        void               explode( const KartProjectile& projectile, const KartItemDef& def );
        int32              pickRedShellTarget( int32 owner ) const;
        void               updatePlaces();
        void               updateRubberBand( int32 racer );
        void               resolveRaceEnd();
        void               pushEvent( KartRaceEvent::Kind kind, int32 racer, int32 other, int32 value, float32 time );

        vector<KartRacer>          _listRacer;
        vector<KartProjectile>     _listProjectile;
        EventBuffer<KartRaceEvent> _eventBuffer;
        vector<Countdown>          _listItemBoxTimer; ///< 끝나 있으면 상자가 놓여 있는 것이다
        vector<int32>              _listPlaceOrder;   ///< 순위 순서로 나열한 차 번호
        KartRaceSettings           _settings;
        GameRandom                 _random;
        FixedStepTimer             _timer;
        const KartTrack*           _pTrack;
        const KartItemCatalog*     _pItemCatalog;
        KartGhost*                 _pGhost;
        float32                    _raceTime;
        Countdown                  _countdown;
        float32                    _bestLapTime;
        float32                    _firstFinishTime; ///< 0 보다 작으면 아직 아무도 들어오지 않은 것이다
        int32                      _ghostRacer;
        int32                      _finishedCount;
        KartRacePhase              _phase;
    };
} // namespace sw
