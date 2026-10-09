/**
 * @file ThemePark.h
 * @brief 테마파크 경영 시뮬레이션(롤러코스터 타이쿤의 손님 · 놀이기구 · 돈 · 공원 평가)입니다.
 *
 * @details 놀이기구는 평가 셋(흥분 · 강도 · 멀미)과 운행 정보(한 번 도는 시간 · 정원 · 값 · 운영비)를 가진 칸입니다. 코스터는 `CoasterRideAnalyzer`
 *          의 시험 운행 결과로 만들고(`makeRideFromCoaster`), 회전목마 같은 평면 놀이기구는 데이터가 평가를 줍니다.
 *          손님은 저마다 견딜 수 있는 강도 범위 · 멀미 내성 · 돈 · 행복 · 기력을 갖고, 놀이기구를 골라 걸어가 줄 서고 타고 내립니다.
 *          화면은 시뮬레이션을 그리기만 합니다 — 손님 자리(`ParkGuest::_position`)도 시뮬레이션이 정합니다. 난수는 씨앗이 같으면 같습니다(시험).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/FixedStepTimer.h"
#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/Base/World/World/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CoasterRideStats;
    struct GameStateRefs;

    class Archive;
    class Wallet;

    // ------------------------------------------------------------------------------
    // 1) 놀이기구
    // ------------------------------------------------------------------------------
    /** @brief 공원에 지은 놀이기구 하나입니다. 운행 상태(줄 · 탑승자 · 타이머)는 시뮬레이션이 씁니다. */
    struct ParkRide
    {
        hashed_string _id{};
        string        _name{};
        float32       _excitement{ 2.0f }; ///< 0..10 — 손님이 끌리는 정도
        float32       _intensity{ 2.0f };  ///< 0..10 — 손님마다 견디는 범위가 있다
        float32       _nausea{ 1.0f };     ///< 0..10 — 탄 뒤 멀미가 오른다
        float32       _cycleTime{ 30.0f }; ///< 한 번 도는 데 걸리는 시간(s, 태우고 내리기 포함)
        float3        _entrance{};         ///< 줄 입구 자리(손님이 걸어온다)
        float3        _footprintCenter{};  ///< 공유 땅에서 얻는 자리의 가운데(XZ)
        float3        _footprintSize{};    ///< 공유 땅에서 얻는 크기(XZ, m) — 0 이면 땅을 얻지 않는다(상태 바이트에 싣지 않는다 — 얻은 칸은 땅이 든다)
        int32         _capacity{ 8 };      ///< 한 번에 태우는 수
        int32         _price{ 3 };         ///< 탑승료
        int32         _runningCostPerMinute{ 5 };
        uint8         _bOpen{ SW_TRUE };

        // ---- 운행 상태(시뮬레이션이 쓴다) ----
        vector<uint32> _listQueue{}; ///< 줄 선 손님 id(앞부터)
        vector<uint32> _listRider{}; ///< 지금 탄 손님 id
        Countdown      _cycleTimer{};
        uint32         _totalRiders{ 0 };
        int32          _totalIncome{ 0 };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 손님
    // ------------------------------------------------------------------------------
    /** @brief 손님이 하는 일입니다. */
    enum class ParkGuestState : uint8
    {
        Walking = 0, ///< 다음 놀이기구(또는 고를 때까지 공원)로 걷는다
        Queuing,     ///< 줄 서 있다
        Riding,      ///< 타고 있다
        Leaving,     ///< 정문으로 걷는다
        Left         ///< 공원을 떠났다(다음 갱신에 목록에서 빠진다)
    };

    /** @brief 손님의 마지막 생각입니다 — 롤러코스터 타이쿤의 "생각" 말풍선 자리. 게임이 세어 보여 준다. */
    enum class ParkGuestThought : uint8
    {
        None = 0,
        GreatRide,    ///< 신나게 탔다
        TooIntense,   ///< 너무 무섭다
        TooTame,      ///< 너무 시시하다
        TooExpensive, ///< 너무 비싸다
        QueueTooLong, ///< 줄이 너무 길다
        Sick,         ///< 멀미가 난다
        Tired,        ///< 지쳤다
        OutOfCash,    ///< 돈이 없다
        NothingToRide ///< 탈 것이 없다
    };

    /** @brief 생각 이름입니다(로그 · UI). */
    SW_GF_API const utf8* toString( ParkGuestThought thought );

    /** @brief 손님 한 명입니다. */
    struct ParkGuest
    {
        float3           _position{};
        float3           _walkFrom{};
        float3           _walkTo{};
        uint32           _id{ 0 };
        int32            _cash{ 0 };
        int32            _targetRideIndex{ -1 }; ///< 걷거나 줄 선 놀이기구(없으면 −1)
        uint32           _rideCount{ 0 };
        float32          _happiness{ 0.7f };    ///< 0..1
        float32          _nausea{ 0.0f };       ///< 0..1
        float32          _energy{ 1.0f };       ///< 0..1 — 다 떨어지면 집에 간다
        float32          _minIntensity{ 1.0f }; ///< 이보다 약하면 시시하다
        float32          _maxIntensity{ 6.0f }; ///< 이보다 세면 타지 않는다
        float32          _nauseaTolerance{ 0.6f };
        float32          _walkTimer{ 0.0f }; ///< 걷기 남은 시간
        float32          _walkDuration{ 1.0f };
        float32          _queueTime{ 0.0f }; ///< 이번 줄에서 기다린 시간
        ParkGuestState   _state{ ParkGuestState::Walking };
        ParkGuestThought _thought{ ParkGuestThought::None };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 설정 · 시뮬레이션
    // ------------------------------------------------------------------------------
    /** @brief 공원 규칙의 상수입니다. 시간은 게임 초입니다. */
    struct ThemeParkSettings
    {
        float3  _gatePosition{};                 ///< 손님이 들어오고 나가는 정문
        float32 _guestArrivalPerMinute{ 12.0f }; ///< 평가 500 · 입장료 0 일 때의 손님 도착률
        float32 _walkSpeed{ 2.5f };              ///< 걷는 속도(m/s) — 걷는 시간은 거리 / 속도
        float32 _wanderRadius{ 6.0f };           ///< 탈 것이 없을 때 놀이기구 입구 둘레로 돌아다니는 반지름(m)
        float32 _queuePatience{ 90.0f };         ///< 줄에서 이만큼 넘게 기다리면 나온다(s)
        float32 _energyDrainPerSecond{ 1.0f / 480.0f };
        float32 _nauseaRecoveryPerSecond{ 1.0f / 120.0f };
        float32 _queueSpacing{ 0.6f }; ///< 줄의 손님 간격(m, 그리기 자리)
        int32   _maxGuests{ 150 };
        int32   _entryFee{ 0 };
        uint32  _randomSeed{ 12345u };
        // ---- 새로 오는 손님의 성향(고르게 뽑는 범위) — 가족 공원 · 스릴 공원을 데이터로 나눈다 ----
        int32         _guestCashMin{ 20 };
        int32         _guestCashMax{ 80 };
        float32       _guestMinIntensityMax{ 3.0f }; ///< "이보다 약하면 시시하다" 의 상한(0 ~ 이 값)
        float32       _guestMaxIntensityMin{ 3.0f }; ///< "이보다 세면 안 탄다" 의 범위
        float32       _guestMaxIntensityMax{ 9.0f };
        float32       _guestNauseaToleranceMin{ 0.3f };
        float32       _guestNauseaToleranceMax{ 0.9f };
        float32       _fixedStep{ 0.25f };   ///< 시뮬레이션 간격(s) — 프레임 수와 상관없이 같은 결과
        float32       _maxFrameTime{ 5.0f }; ///< 한 프레임에 받는 시간 상한(빨리 감기 포함)
        hashed_string _currency{ "Cash" };   ///< 빌린 지갑에서 쓰는 공원 돈의 통화
    };
} // namespace sw

namespace sw
{
    /**
     * @class ThemeParkSimulation
     * @brief 공원 하나 — 놀이기구 · 손님 · 돈 · 평가를 시간으로 돌립니다.
     * @details 손님의 규칙(롤러코스터 타이쿤을 줄인 것):
     *          - 도착: 분당 `_guestArrivalPerMinute` × (0.4 + 평가/1000) × 입장료 감쇠(입장료 100 이면 0). 들어올 때 입장료를 낸다.
     *          - 고르기: 열린 놀이기구 중 강도가 자기 상한 이하 · 멀미 내성 안 · 값이 가치(흥분 × 2) 의 두 배 이하 · 줄이 참을 만한 것 가운데
     *            흥분이 높은 것(약간의 무작위). 고를 것이 없으면 행복이 줄고 마지막 거절 이유를 생각한다.
     *          - 탑승: 놀이기구가 쉬고 있으면 줄 앞에서 정원만큼 태우고(탑승료를 낸다 — 돈이 없으면 떠난다) 한 바퀴를 돈다. 내리면 행복이 흥분만큼,
     *            멀미가 멀미 평가만큼 오르고 기력이 준다. 강도가 자기 하한보다 약했으면 덜 즐겁다.
     *          - 떠남: 기력이 바닥 · 행복이 바닥 · 돈이 바닥이면 정문으로 걸어가 떠난다.
     *          운영비는 열린 놀이기구마다 분당 `_runningCostPerMinute` 씩 나간다.
     */
    class SW_GF_API ThemeParkSimulation
    {
    public:
        ThemeParkSimulation();

        /**
         * @brief 설정으로 빈 공원을 엽니다. 공원 돈은 빌린 지갑(@p refs 의 지갑 — 통화 `ThemeParkSettings::_currency`)이고 시작 자금은 게임이 넣습니다.
         * @details 짓기는 `trySpend`, 입장료 · 탈것 요금은 `add`, 운영비는 `charge`(빚이 될 수 있다). 지갑이 없으면 짓기는 모두 거절, 수입은 버립니다.
         */
        void initialize( const ThemeParkSettings& settings, const GameStateRefs& refs );
        /** @brief 시간을 흘립니다. 큰 시간은 0.25 초씩 나눠 돈다. */
        void update( float32 deltaTime );

        /**
         * @brief 공유 땅을 빌립니다(월드 원점이 땅의 원점 · 칸 크기를 따른다 — 공원은 칸 격자가 없어 월드 사각으로 얻는다). @p pLand 가 nullptr 이면 풉니다.
         * @details 그 뒤로 놀이기구는 지을 때 발자국(`_footprintCenter` · `_footprintSize`)을 막힘으로 얻습니다.
         */
        void bindLand( LandRegistry* pLand );
        /** @brief 놀이기구를 짓습니다. 지갑에 @p buildCost 가 없거나 발자국의 공유 땅이 남의 것이면 짓지 않고 −1 입니다. 지은 칸 번호를 돌려줍니다. */
        int32 buildRide( const ParkRide& ride, int32 buildCost );
        /** @brief 놀이기구를 닫거나 엽니다. 닫으면 줄 선 손님은 나와 다른 것을 고른다. */
        void setRideOpen( int32 rideIndex, bool bOpen );
        void setRidePrice( int32 rideIndex, int32 price );
        void setEntryFee( int32 entryFee ) { _settings._entryFee = entryFee < 0 ? 0 : entryFee; }
        /** @brief 손님 하나를 정문에 바로 들입니다(시험 · 이벤트). 입장료를 받는다. 정원이 찼으면 false 입니다. */
        [[nodiscard]] bool admitGuest( int32 cash, float32 minIntensity, float32 maxIntensity, float32 nauseaTolerance );

        /**
         * @brief 코스터 시험 운행 결과로 놀이기구를 만듭니다. 한 번 도는 시간은 한 바퀴 시간 + @p loadTime 입니다.
         * @details 한 바퀴를 못 도는 코스터(`_bCompleted` 가 아님)는 흥분 0 이라 아무도 타려 하지 않는다 — 타이쿤과 같다.
         */
        static ParkRide makeRideFromCoaster( const hashed_string& id, const string& name, const CoasterRideStats& stats, int32 capacity, float32 loadTime );
        /** @brief 손님이 생각하는 놀이기구 가치(적정 탑승료)입니다 — 흥분 × 2. */
        static float32 computeRideValue( const ParkRide& ride );

        int32                    getParkRating() const { return _parkRating; }
        const vector<ParkRide>&  getRides() const { return _listRide; }
        const vector<ParkGuest>& getGuests() const { return _listGuest; }
        /** @brief 공원 안의 손님 수입니다(떠나는 중 포함, 떠난 사람 제외). */
        uint32  getGuestCount() const;
        uint32  getTotalVisitorCount() const { return _totalVisitorCount; }
        float32 getAverageHappiness() const;
        float32 getElapsedTime() const { return _elapsedTime; }
        /** @brief 지금 공원 안 손님 중 그 생각을 하는 수입니다. */
        uint32                   countGuestsThinking( ParkGuestThought thought ) const;
        const ThemeParkSettings& getSettings() const { return _settings; }

        /**
         * @brief 놀이기구(운행 상태 포함) · 손님 · 돈 · 평가 · 난수 · 입장료를 씁니다(핫 리로드 · 세이브). 나머지 설정은 쓰지 않습니다 — 읽는 쪽이 같은 것으로 `initialize` 합니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void stepFixed( float32 deltaTime );
        void spawnGuests( float32 deltaTime );
        void updateGuests( float32 deltaTime );
        void runRides( float32 deltaTime );
        void chooseNextRide( ParkGuest& guest );
        void startWalking( ParkGuest& guest, const float3& destination );
        void sendHome( ParkGuest& guest, ParkGuestThought thought );
        void leaveQueue( ParkGuest& guest );
        void updateParkRating();
        /** @brief id 로 손님을 찾습니다. 목록은 id 오름차순이라(새 손님은 뒤에 붙고 지울 때 순서를 지킨다) 이분 탐색입니다. */
        ParkGuest* findGuest( uint32 guestId );
        float32    nextRandom() { return _random.nextFloat(); }

        ThemeParkSettings _settings;
        vector<ParkRide>  _listRide;
        vector<ParkGuest> _listGuest;
        RateAccumulator   _arrival;     ///< 손님 도착(명)
        RateAccumulator   _runningCost; ///< 운영비(돈)
        float32           _elapsedTime;
        FixedStepTimer    _stepTimer;
        GameRandom        _random;
        Wallet*           _pWallet; ///< 빌린 지갑(공원 돈)
        LandRegistry*     _pLand;   ///< 빌린 공유 땅(없으면 단독)
        int32             _parkRating;
        uint16            _landOwner;
        uint32            _nextGuestId;
        uint32            _totalVisitorCount;
    };
} // namespace sw
