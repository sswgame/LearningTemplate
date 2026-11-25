/**
 * @file ThemePark.h
 * @brief 테마파크 경영 시뮬레이션을 구현합니다. 롤러코스터 타이쿤의 손님, 놀이기구, 돈, 공원 평가를 다룹니다.
 *
 * @details 놀이기구는 평가 셋(흥분, 강도, 멀미)과 운행 정보(한 번 도는 시간, 정원, 가격, 운영비)를 가진다. 코스터는 `CoasterRideStats`
 *          의 테스트 운행 결과로 만들고(`makeRideFromCoaster`), 회전목마 같은 평면 놀이기구는 데이터가 평가를 준다.
 *          손님은 저마다 견딜 수 있는 강도 범위, 멀미 내성, 돈, 행복, 기력을 가지고, 놀이기구를 골라 걸어가 줄을 서고 타고 내린다.
 *          화면은 시뮬레이션을 그리기만 한다. 손님의 위치(`ParkGuest::_position`)도 시뮬레이션이 정한다. 난수는 시드가 같으면 같은 결과가 나온다(테스트용).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Navigation/GridPathfinder.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/World/Land/LandRegistry.h"

namespace sw
{
    struct CoasterRideStats;

    class Archive;
    class NavGrid;
    class Wallet;

    // ------------------------------------------------------------------------------
    // 1) 놀이기구
    // ------------------------------------------------------------------------------
    /** @brief 공원에 지은 놀이기구 하나입니다. 운행 상태(줄, 탑승자, 타이머)는 시뮬레이션이 씁니다. */
    REFLECT()
    struct ParkRide
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        string _name{};
        PROPERTY( Min = 0 )
        float32 _excitement{ 2.0f }; ///< 0..10 범위이며 손님이 끌리는 정도이다
        PROPERTY( Min = 0 )
        float32 _intensity{ 2.0f }; ///< 0..10 범위이며 손님마다 견디는 범위가 있다
        PROPERTY( Min = 0 )
        float32 _nausea{ 1.0f }; ///< 0..10 범위이며 탄 뒤 멀미가 오른다
        PROPERTY( Min = 1 )
        float32 _cycleTime{ 30.0f }; ///< 한 번 도는 데 걸리는 시간이다(s, 태우고 내리기 포함). `buildRide` 가 1 이상으로 맞춘다
        PROPERTY()
        float3 _entrance{};        ///< 줄 입구 위치이다(손님이 걸어온다)
        float3 _footprintCenter{}; ///< 공유 땅에서 얻는 영역의 가운데(XZ)
        float3 _footprintSize{};   ///< 공유 땅에서 얻는 크기이다(XZ, m). 0 이면 땅을 얻지 않는다. 얻은 셀은 땅이 가지고 있으므로 상태 바이트에 싣지 않는다
        PROPERTY( Min = 1 )
        int32 _capacity{ 8 }; ///< 한 번에 태우는 수(0 은 줄이 영영 줄지 않고 예상 대기가 0 나누기가 된다)
        PROPERTY( Min = 0 )
        int32 _price{ 3 }; ///< 탑승료
        PROPERTY( Min = 0 )
        int32 _runningCostPerMinute{ 5 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bOpen{ SW_TRUE };

        // ---- 운행 상태(시뮬레이션이 쓴다) ----
        PROPERTY()
        vector<uint32> _listQueue{};      ///< 줄 선 손님 id(앞에서부터)
        vector<float3> _listQueuePoint{}; ///< 줄이 늘어서는 길의 점(입구에서 정문까지)이다. 처음 줄을 설 때 걷는 격자에서 구하는 사본이라 상태 바이트에 싣지 않는다
        PROPERTY()
        vector<uint32> _listRider{};  ///< 지금 탄 손님 id
        Countdown      _cycleTimer{}; ///< 리플렉션 밖 값 — 남은 시간은 시뮬레이션 본문이 손으로 싣는다
        PROPERTY()
        uint32 _totalRiders{ 0 };
        PROPERTY( Min = 0 )
        int32 _totalIncome{ 0 };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 손님
    // ------------------------------------------------------------------------------
    /** @brief 손님이 하는 일입니다. */
    ENUM()
    enum class ParkGuestState : uint8
    {
        Walking = 0, ///< 다음 놀이기구(또는 고를 때까지 공원)로 걷는다
        Queuing,     ///< 줄 서 있다
        Riding,      ///< 타고 있다
        Leaving,     ///< 정문으로 걷는다
        Left         ///< 공원을 떠났다(다음 갱신에 목록에서 빠진다)
    };

    /** @brief 손님의 마지막 생각입니다. 롤러코스터 타이쿤의 "생각" 말풍선에 해당하며, 게임이 세어서 보여 줍니다. */
    ENUM()
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

    /** @brief 손님 한 명입니다. */
    REFLECT()
    struct ParkGuest
    {
        REFLECT_BODY();

        PROPERTY()
        float3 _position{};
        PROPERTY()
        float3 _walkFrom{};
        PROPERTY()
        float3 _walkTo{}; ///< 지금 걷는 다리의 끝
        PROPERTY()
        vector<float3> _listWalkPoint{}; ///< `_walkTo` 다음에 지날 점(마지막이 목적지)이다. 비면 `_walkTo` 가 목적지다
        PROPERTY( Min = 1 )
        uint32 _id{ 0 };
        PROPERTY( Min = 0 )
        int32 _cash{ 0 };
        PROPERTY( Min = -1 )
        int32 _targetRideIndex{ constant::invalid_index::kInt32 }; ///< 걷거나 줄 선 놀이기구이다(없으면 -1)
        PROPERTY()
        uint32 _rideCount{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        float32 _happiness{ 0.7f }; ///< 0..1 범위
        PROPERTY( Min = 0, Max = 1 )
        float32 _nausea{ 0.0f }; ///< 0..1 범위
        PROPERTY( Min = 0, Max = 1 )
        float32 _energy{ 1.0f }; ///< 0..1 범위이며 다 떨어지면 집에 간다
        PROPERTY()
        float32 _minIntensity{ 1.0f }; ///< 이보다 약하면 시시하다고 느낀다
        PROPERTY( Min = "_minIntensity" )
        float32 _maxIntensity{ 6.0f }; ///< 이보다 세면 타지 않는다
        PROPERTY()
        float32 _nauseaTolerance{ 0.6f };
        PROPERTY()
        float32 _walkTimer{ 0.0f }; ///< 걷기가 끝나기까지 남은 시간
        PROPERTY( Min = 0 )
        float32 _walkDuration{ 1.0f };
        PROPERTY( Min = 0 )
        float32 _queueTime{ 0.0f }; ///< 이번 줄에서 기다린 시간
        PROPERTY()
        ParkGuestState _state{ ParkGuestState::Walking };
        PROPERTY()
        ParkGuestThought _thought{ ParkGuestThought::None };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 설정과 시뮬레이션
    // ------------------------------------------------------------------------------
    /**
     * @brief 공원 규칙의 상수입니다. 시간 단위는 게임 초입니다.
     * @details 규칙 자산이다 — 공원 배치(`ParkLayout`)의 `<Rules …/>` 한 줄이 모든 칸을 적는다(`Required`). 정문 · 입장료만 배치 루트가 정한다.
     *          범위는 필드의 `PROPERTY( Min )` 이고 `ThemeParkSimulation::initialize` 가 벗어난 값을 거절한다(`ConfigRangeUtil`) — 나누는 칸(입장료 0 도착 · 평가 폭 · 걷는 속도 ·
     *          걸음)이 0 이면 도착률 · 걷는 시간이 무한대 · NaN 이 되어 공원이 조용히 멈췄다(감사 K-4). 하한 · 상한 짝(손님 돈 · 강도 · 멀미 참기)은 읽기가 교차로 본다.
     */
    REFLECT()
    struct SW_KIT_API ThemeParkSettings
    {
        REFLECT_BODY();
        float3 _gatePosition{}; ///< 손님이 들어오고 나가는 정문
        PROPERTY( Required, Min = 0.0 )
        float32 _guestArrivalPerMinute{ 12.0f }; ///< 평가 500, 입장료 0 일 때의 손님 도착률
        PROPERTY( Required, Min = 0.01 )
        float32 _walkSpeed{ 2.5f }; ///< 걷는 속도(m/s)이다. 걷는 시간은 거리 / 속도이다
        PROPERTY( Required, Min = 0.0 )
        float32 _wanderRadius{ 6.0f }; ///< 탈 것이 없을 때 놀이기구 입구 둘레를 돌아다니는 반지름(m)
        PROPERTY( Required, Min = 0.0 )
        float32 _queuePatience{ 90.0f }; ///< 줄에서 이 시간(s)을 넘게 기다리면 줄에서 나온다
        PROPERTY( Required, Min = 0.0 )
        float32 _energyDrainPerSecond{ 1.0f / 480.0f };
        PROPERTY( Required, Min = 0.0 )
        float32 _nauseaRecoveryPerSecond{ 1.0f / 120.0f };
        PROPERTY( Required, Min = 0.0 )
        float32 _queueSpacing{ 0.6f }; ///< 줄에 선 손님의 간격(m)이다(그리기 위치)
        PROPERTY( Required, Min = 0.0 )
        float32 _leaveEnergy{ 0.1f }; ///< 기력이 이보다 낮으면 떠난다
        PROPERTY( Required, Min = 0.0 )
        float32 _leaveHappiness{ 0.15f }; ///< 행복이 이보다 낮으면 떠난다
        PROPERTY( Required, Min = 0.0 )
        float32 _nauseaPerRating{ 0.06f }; ///< 멀미 평가 1 이 손님 멀미에 더하는 양
        PROPERTY( Required, Min = 0.0 )
        float32 _queueUnhappinessPerSecond{ 0.002f }; ///< 줄에서 초당 줄어드는 행복
        PROPERTY( Required, Min = 0 )
        int32 _maxGuests{ 150 };
        int32 _entryFee{ 0 };
        PROPERTY( Required, Min = 0 )
        uint32 _randomSeed{ 12345u };
        // ---- 새로 오는 손님의 성향(고르게 뽑는 범위). 가족 공원과 스릴 공원을 데이터로 나눈다 ----
        PROPERTY( Required, Min = 0 )
        int32 _guestCashMin{ 20 };
        PROPERTY( Required, Min = 0 )
        int32 _guestCashMax{ 80 };
        PROPERTY( Required, Min = 0.0 )
        float32 _guestMinIntensityMax{ 3.0f }; ///< "이보다 약하면 시시하다" 의 상한이다(0 ~ 이 값)
        PROPERTY( Required, Min = 0.0 )
        float32 _guestMaxIntensityMin{ 3.0f }; ///< "이보다 세면 안 탄다" 의 범위이다
        PROPERTY( Required, Min = 0.0 )
        float32 _guestMaxIntensityMax{ 9.0f };
        PROPERTY( Required, Min = 0.0 )
        float32 _guestNauseaToleranceMin{ 0.3f };
        PROPERTY( Required, Min = 0.0 )
        float32 _guestNauseaToleranceMax{ 0.9f };
        // ---- 손님 도착 · 놀이기구 만족 · 공원 평가 공식의 계수(공원의 성격은 코드가 아니라 이 칸이다) ----
        PROPERTY( Required, Min = 0.0 )
        float32 _arrivalBaseFactor{ 0.4f }; ///< 평가 0 일 때의 도착률 배율이다(평가가 `_arrivalRatingScale` 오를 때마다 1 씩 는다)
        PROPERTY( Required, Min = 1.0 )
        float32 _arrivalRatingScale{ 1000.0f }; ///< 도착률 배율이 1 오르는 평가 폭
        PROPERTY( Required, Min = 0.0 )
        float32 _noRideArrivalFactor{ 0.2f }; ///< 놀이기구가 없는 공원의 도착률 배율(거의 오지 않는다)
        PROPERTY( Required, Min = 1.0 )
        float32 _noArrivalEntryFee{ 100.0f }; ///< 이 입장료에서 도착률이 0 이 된다(사이는 직선)
        PROPERTY( Required, Min = 0.0 )
        float32 _rideFunBase{ 0.04f }; ///< 한 번 탈 때 오르는 행복의 바탕
        PROPERTY( Required, Min = 0.0 )
        float32 _rideFunPerExcitement{ 0.025f }; ///< 흥분 평가 1 이 더하는 행복
        PROPERTY( Required, Min = 0.0 )
        float32 _greatRideExcitement{ 6.0f }; ///< 이 흥분 평가 이상이면 "멋진 놀이기구" 라 생각한다
        PROPERTY( Required, Min = 0.0 )
        float32 _tameRideFunScale{ 0.5f }; ///< 시시한 놀이기구에서 얻는 행복의 배율
        PROPERTY( Required, Min = 0.0 )
        float32 _sickFunPenalty{ 0.1f }; ///< 탄 뒤 멀미가 참을 선을 넘으면 깎이는 행복
        PROPERTY( Required, Min = 0.0 )
        float32 _rideEnergyCost{ 0.04f }; ///< 한 번 탈 때 드는 기력
        PROPERTY( Required, Min = 0.0 )
        float32 _queueGiveUpUnhappiness{ 0.1f }; ///< 줄을 포기할 때 깎이는 행복
        PROPERTY( Required, Min = 0.0 )
        float32 _nothingToRideUnhappiness{ 0.05f }; ///< 탈 것을 못 찾을 때 깎이는 행복
        PROPERTY( Required, Min = 0.0 )
        float32 _leaveNausea{ 0.95f }; ///< 멀미가 이보다 크면 떠난다
        PROPERTY( Required, Min = 0.0 )
        float32 _fairPriceValueScale{ 2.0f }; ///< 표값이 가치 × 이 배율 + `_fairPriceSlack` 을 넘으면 비싸서 타지 않는다
        PROPERTY( Required, Min = 0.0 )
        float32 _fairPriceSlack{ 0.5f };
        PROPERTY( Required, Min = 0.0 )
        float32 _rideChoiceJitter{ 1.5f }; ///< 놀이기구 고르기 점수에 더하는 무작위 폭(0 ~ 이 값)
        PROPERTY( Required, Min = 0.0 )
        float32 _tameRideChoicePenalty{ 2.0f }; ///< 시시하지만 탈 수는 있는 놀이기구의 점수 감점
        PROPERTY( Required, Min = 0.0 )
        float32 _repeatRideChoicePenalty{ 1.5f }; ///< 방금 탄 놀이기구의 점수 감점(같은 것만 타지 않게)
        PROPERTY( Required, Min = 0.0 )
        float32 _minWalkSeconds{ 0.5f }; ///< 아무리 가까워도 걷는 데 드는 시간(s)
        PROPERTY( Required, Min = 0.0 )
        float32 _ratingPerOpenRide{ 25.0f }; ///< 열린 놀이기구 하나가 평가에 더하는 점수
        PROPERTY( Required, Min = 0 )
        uint32 _ratedOpenRideCount{ 8u }; ///< 평가에 세는 열린 놀이기구 수의 상한
        PROPERTY( Required, Min = 0.0 )
        float32 _emptyParkRating{ 400.0f }; ///< 손님이 없을 때의 평가 바탕
        PROPERTY( Required, Min = 0.0 )
        float32 _ratingBase{ 200.0f }; ///< 손님이 있을 때의 평가 바탕
        PROPERTY( Required, Min = 0.0 )
        float32 _ratingPerHappiness{ 600.0f }; ///< 평균 행복 1 이 더하는 평가
        PROPERTY( Required, Min = 0.0 )
        float32 _ratingPerNausea{ 300.0f }; ///< 평균 멀미 1 이 깎는 평가
        PROPERTY( Required, Min = 0.0 )
        float32 _ratingPerQueueComplaint{ 100.0f }; ///< 줄이 길다고 생각하는 손님 비율 1 이 깎는 평가
        PROPERTY( Required, Min = 0.001 )
        float32 _fixedStep{ 0.25f }; ///< 시뮬레이션 간격(s)이다. 프레임 수와 상관없이 같은 결과가 나온다
        PROPERTY( Required, Min = 0.001 )
        float32 _maxFrameTime{ 5.0f }; ///< 한 프레임에 받는 시간의 상한이다(빨리 감기 포함)
        PROPERTY( Required )
        hashed_string _currency{}; ///< 빌린 지갑에서 쓰는 공원 돈의 통화
    };
} // namespace sw

namespace sw
{
    /**
     * @class ThemeParkSimulation
     * @brief 공원 하나를 시뮬레이션합니다. 놀이기구, 손님, 돈, 평가를 시간에 따라 진행합니다.
     * @details 손님의 규칙은 롤러코스터 타이쿤을 줄인 것이다.
     *          - 도착: 분당 `_guestArrivalPerMinute` x (0.4 + 평가/1000) x 입장료 감쇠(입장료 100 이면 0)이다. 들어올 때 입장료를 낸다.
     *          - 고르기: 열린 놀이기구 중에서 강도가 자기 상한 이하이고, 멀미 내성 안이고, 가격이 가치(흥분 x 2)의 두 배 이하이고, 줄이 참을 만한 것 가운데
     *            흥분이 높은 것을 고른다(약간의 무작위가 있다). 고를 것이 없으면 행복이 줄고 마지막 거절 이유를 생각한다.
     *          - 탑승: 놀이기구가 쉬고 있으면 줄 앞에서 정원만큼 태우고(탑승료를 내며, 돈이 없으면 떠난다) 한 바퀴를 돈다. 내리면 행복이 흥분만큼,
     *            멀미가 멀미 평가만큼 오르고 기력이 준다. 강도가 자기 하한보다 약했으면 덜 즐겁다.
     *          - 떠남: 기력, 행복, 돈 중 하나가 바닥이면 정문으로 걸어가 떠난다.
     *          운영비는 열린 놀이기구마다 분당 `_runningCostPerMinute` 씩 나간다.
     */
    REFLECT()
    class SW_KIT_API ThemeParkSimulation
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kMaxParkRating = 999; ///< 공원 평가의 위 끝(상태 읽기가 검사하는 범위)

        ThemeParkSimulation();

        /**
         * @brief 설정으로 빈 공원을 엽니다. 공원 돈은 빌린 지갑(@p wallet — 통화는 `ThemeParkSettings::_currency`)이고, 시작 자금은 게임이 넣습니다.
         * @param walkGrid 손님이 돌아갈 장애물(가판 · 의자 · 지을 땅)의 격자입니다. 시뮬레이션보다 오래 산다. 장애물이 없는 공원은 칸이 없는 격자를 준다 — 그때 곧게 걷는다.
         * @details 건설은 `trySpend`, 입장료와 탈것 요금은 `add`, 운영비는 `charge`(빚이 될 수 있다)로 처리한다.
         * @return 설정이 범위 밖이거나 하한이 상한보다 크면 false 이고 공원을 열지 않는다(오류 로그).
         */
        [[nodiscard]] bool initialize( const ThemeParkSettings& settings, Wallet& wallet, const NavGrid& walkGrid );
        /** @brief 시간을 진행합니다. 큰 시간은 0.25초씩 나눠 처리합니다. */
        void update( float32 deltaTime );

        /**
         * @brief 공유 땅을 빌립니다. 월드 원점이 땅의 원점과 셀 크기를 따릅니다(공원은 셀 격자가 없으므로 월드 사각형으로 얻습니다).
         * @details 그 뒤로 놀이기구는 지을 때 영역(`_footprintCenter`, `_footprintSize`)을 막힘으로 얻는다.
         */
        void bindLand( LandRegistry& land );
        /**
         * @brief @p from 에서 @p to 로 걷는 길의 점(시작은 뺀다, 마지막이 @p to)을 @p outListPoint 에 채웁니다. 곧게 지날 수 있거나 격자 밖이면 @p to 하나입니다.
         * @details 손님의 걸음과 깔리는 길의 모습이 같은 함수를 부른다 — 길은 손님이 걷는 자리에 놓인다.
         */
        static void findWalkRoute( const NavGrid& grid, GridPathfinder& inoutPathfinder, const float3& from, const float3& to, vector<float3>& outListPoint );
        /** @brief 놀이기구를 짓고 놀이기구 index 를 반환합니다. 지갑에 @p buildCost 가 없거나 영역의 공유 땅이 남의 것이면 짓지 않고 -1 을 반환합니다. */
        int32 buildRide( const ParkRide& ride, int32 buildCost );
        /** @brief 놀이기구를 닫거나 엽니다. 닫으면 줄 선 손님은 줄에서 나와 다른 것을 고릅니다. */
        void setRideOpen( int32 rideIndex, bool bOpen );
        void setRidePrice( int32 rideIndex, int32 price );
        void setEntryFee( int32 entryFee ) { _settings._entryFee = entryFee < 0 ? 0 : entryFee; }
        /** @brief 손님 하나를 정문에 바로 들입니다(테스트와 이벤트용). 입장료를 받으며, 정원이 찼으면 false 를 반환합니다. */
        [[nodiscard]] bool admitGuest( int32 cash, float32 minIntensity, float32 maxIntensity, float32 nauseaTolerance );

        /**
         * @brief 코스터 테스트 운행 결과로 놀이기구를 만듭니다. 한 번 도는 시간은 한 바퀴 시간 + @p loadTime 입니다.
         * @details 한 바퀴를 못 도는 코스터(`_bCompleted` 가 아님)는 흥분이 0 이라 아무도 타려 하지 않는다. 타이쿤과 같다.
         */
        static ParkRide makeRideFromCoaster( const hashed_string& id, const string& name, const CoasterRideStats& stats, int32 capacity, float32 loadTime );
        /** @brief 손님이 생각하는 놀이기구 가치(적정 탑승료)를 반환합니다. 흥분 x 2 입니다. */
        static float32 computeRideValue( const ParkRide& ride );

        int32                    getParkRating() const { return _parkRating; }
        const vector<ParkRide>&  getRides() const { return _listRide; }
        const vector<ParkGuest>& getGuests() const { return _listGuest; }
        /** @brief 공원 안의 손님 수를 반환합니다(떠나는 중인 손님은 포함하고, 떠난 손님은 제외합니다). */
        uint32  getGuestCount() const;
        uint32  getTotalVisitorCount() const { return _totalVisitorCount; }
        float32 getAverageHappiness() const;
        float32 getElapsedTime() const { return _elapsedTime; }
        /** @brief 지금 공원 안 손님 중 그 생각을 하는 수를 반환합니다. */
        uint32                   countGuestsThinking( ParkGuestThought thought ) const;
        const ThemeParkSettings& getSettings() const { return _settings; }

        /**
         * @brief 놀이기구(운행 상태 포함), 손님, 돈, 평가, 난수, 입장료를 씁니다(핫 리로드와 세이브용). 나머지 설정은 쓰지 않으며, 읽는 쪽이 같은 설정으로 `initialize` 해야 합니다.
         *        `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 음수 · 범위 밖 값, 손님과 맞지 않는 줄과 탑승자가 있거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        void stepFixed( float32 deltaTime );
        void spawnGuests( float32 deltaTime );
        void updateGuests( float32 deltaTime );
        void runRides( float32 deltaTime );
        void chooseNextRide( ParkGuest& guest );
        /** @brief @p destination 까지의 길을 정하고 첫 다리를 걷기 시작합니다. */
        void startWalking( ParkGuest& guest, const float3& destination );
        /** @brief @p to 까지의 다리 하나를 걷기 시작합니다. 아무리 짧아도 @p minSeconds 는 걸린다. */
        void startWalkLeg( ParkGuest& guest, const float3& to, float32 minSeconds );
        /** @brief 놀이기구 @p inoutRide 의 줄에서 앞에서 @p lineIndex 번째(0 부터) 손님의 자리입니다 — 입구에서 정문으로 가는 걷는 길을 따라 늘어서고 정문을 넘지 않는다. */
        float3 computeQueuePosition( ParkRide& inoutRide, size_t lineIndex );
        void   sendHome( ParkGuest& guest, ParkGuestThought thought );
        void   leaveQueue( ParkGuest& guest );
        void   updateParkRating();
        /** @brief id 로 손님을 찾습니다. 목록이 id 오름차순이므로(새 손님은 뒤에 붙고 지울 때 순서를 지킵니다) 이분 탐색을 씁니다. */
        ParkGuest* findGuest( uint32 guestID );
        float32    nextRandom() { return _random.nextFloat(); }

    private:
        ThemeParkSettings _settings;
        PROPERTY( RuntimeState )
        vector<ParkRide> _listRide;
        PROPERTY( RuntimeState )
        vector<ParkGuest> _listGuest;
        RateAccumulator   _arrival;     ///< 손님 도착(명)
        RateAccumulator   _runningCost; ///< 운영비(돈)
        PROPERTY( RuntimeState, Min = 0 )
        float32        _elapsedTime;
        FixedStepTimer _stepTimer;
        GameRandom     _random;
        Wallet*        _pWallet;    ///< 빌린 지갑(공원 돈 — `initialize` 뒤 늘 있다)
        LandRegistry*  _pLand;      ///< 빌린 공유 땅(없으면 단독 동작)
        const NavGrid* _pWalkGrid;  ///< 빌린 걷기 격자(`initialize` 뒤 늘 있다)
        GridPathfinder _pathfinder; ///< 길 찾기의 작업 배열 — 싣지 않는다
        PROPERTY( RuntimeState, Min = 0, Max = ThemeParkSimulation::kMaxParkRating )
        int32  _parkRating;
        uint16 _landOwner;
        PROPERTY( RuntimeState, Min = 1 )
        uint32 _nextGuestID;
        PROPERTY( RuntimeState )
        uint32 _totalVisitorCount;
    };
} // namespace sw
