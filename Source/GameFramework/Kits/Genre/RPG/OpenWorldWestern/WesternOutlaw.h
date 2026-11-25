/**
 * @file WesternOutlaw.h
 * @brief 플레이어 한 명의 서부극 삶입니다. 명예(`WesternHonor`), 법(`WesternLawState`), 생존(`WesternSurvival`), 사냥한 사체와 가죽을 한 지역에 묶어 시계로 흘리고, 행동(`WesternDeed`) 하나를 규칙들에 나눠 줍니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternCarcass.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternHonor.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternLaw.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternSurvival.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    class Archive;
    class GameFlags;
    class Inventory;
    class LootCatalog;
    class ReputationState;
    class Wallet;
    class WeatherSystem;
    class WorldClock;

    /** @brief 플레이어가 세상에 하는 일 하나의 종류입니다(씬의 `WesternDeedComponent` 가 데이터로 고른다). */
    ENUM()
    enum class WesternDeedKind : uint8
    {
        Crime = 0,     ///< 범죄를 저지른다(`_id` 는 `<Crime>`). 근처 사람이 보면 신고된다
        HonorAction,   ///< 이름 붙은 행동(`_id` 는 `<Honor><Action>`)
        Hunt,          ///< 사냥감을 잡는다(`_id` 는 `<Animal>`, 무기 · 부위 · 맞힌 수)
        Skin,          ///< 가진 사체의 가죽을 모두 벗긴다
        SellPelts,     ///< 가죽을 모두 판다
        PayBounty,     ///< 이 지역 현상금을 낸다
        Eat,           ///< 먹는다(`_id` 는 `<Food>`)
        ChangeClothes, ///< 갈아입는다(`_id` 는 `<Clothing>`). 변장이라 수배가 빨리 식는다
    };
} // namespace sw

namespace sw
{
    /** @brief 행동 하나입니다. */
    struct WesternDeed
    {
        hashed_string   _id{};
        hashed_string   _weapon{};
        hashed_string   _zone{};
        float3          _position{}; ///< 일어난 자리(범죄 목격 판정)
        int32           _hitCount{ 1 };
        WesternDeedKind _kind{ WesternDeedKind::HonorAction };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WesternOutlaw
     * @brief 게임 디렉터가 하나 가지는 보통 클래스입니다(세이브는 디렉터가 `writeState` 로 싣는다). 지갑 · 평판 · 시계 · 플래그 · 날씨는 빌려 씁니다(필수 — 참조). 전리품 표 · 가방은 동물 정의가 켠다(`loot` 를 적은 동물이 있으면 있어야 한다).
     * @details 시간은 빌린 시계로 잰다 — `update` 가 지난번 뒤로 흐른 게임 시간과 넘은 날을 구해 생존 · 사체 · 현상금에 넘긴다. 잠과 빨리 감기(`AdvanceGameTime`)도 같은 길로 닿는다.
     *          돈의 단위는 카탈로그의 통화 하나다. 가죽 값(`WesternCarcass`)은 센트이므로 팔 때 달러로 내리고 남은 센트는 다음 판매로 넘긴다. 명예 단계의 할인 비율은 파는 값에 웃돈으로 얹는다.
     *          레드 데드 리뎀션 2 의 명예 · 현상금 · 코어 흐름을 따랐다(기억에 의존한 비교).
     */
    REFLECT()
    class SW_KIT_API WesternOutlaw
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "WOTL" );
        static constexpr uint32 kStateVersion = 2; // 2: 전리품 난수 · 지난 시계를 싣는다

        WesternOutlaw();

        /**
         * @brief 지역 @p regionID 에서 시작합니다. @p regionBaseTemperature 는 날씨 값을 더하기 전의 기온(섭씨)이고 @p sight 는 목격자의 시야입니다.
         * @param pLoot 손질한 사체의 전리품 테이블(동물의 `loot`)을 굴릴 카탈로그입니다. 굴린 것은 빌린 가방(@p pInventory)에 든다.
         *              전리품 테이블을 적은 동물이 없는 카탈로그면 둘 다 nullptr 이어도 된다 — 적은 동물이 있는데 없으면 시작하지 않는다.
         * @return 카탈로그가 없거나, 전리품을 적은 동물이 있는데 전리품 카탈로그 · 가방이 없거나, 지역이 카탈로그에 없거나, 생존 수치를 열지 못하면 오류를 남기고 false 입니다(시작하지 않는다).
         */
        [[nodiscard]] bool initialize( const WesternCatalog* pCatalog, const LootCatalog* pLoot, Wallet& wallet, ReputationState& reputation, WorldClock& clock, GameFlags& flags,
                                       WeatherSystem& weather, Inventory* pInventory, const hashed_string& regionID, float32 regionBaseTemperature,
                                       const AIPerceptionSettings& sight );
        /** @brief 빌린 카탈로그만 @p catalog 로 바꿉니다(지갑 · 평판 · 시계는 그대로). 법 · 생존 · 동물은 이어 읽는 `readState` 가 새 판으로 다시 연다 — 읽기 앞에서만 부른다. */
        void rebindCatalog( const WesternCatalog& catalog );
        /** @brief 실제 @p deltaTime 초와 빌린 시계가 지난번 뒤로 흐른 만큼 흘립니다. 대사 플래그를 맞춥니다. */
        void update( GameSeconds frameTime );
        /** @brief 행동을 규칙에 넘깁니다. @p listCandidate 는 범죄를 볼 수 있는 후보입니다. 받아들였으면 true 입니다(모르는 id · 벗길 사체 없음 · 낼 현상금 없음 · 돈 모자람은 false). */
        [[nodiscard]] bool perform( const WesternDeed& deed, const vector<WesternWitness>& listCandidate );
        /** @brief 법이 지금 플레이어를 보는지 알립니다(보는 동안 수배가 식지 않는다). 후보 가운데 보안관이 플레이어 자리 @p position 을 보면 그렇습니다. */
        void updateSeenByLaw( const float3& position, const vector<WesternWitness>& listCandidate );

        int32                  getHonorValue() const { return _honor.getValue(); }
        hashed_string          getHonorTierName() const { return _honor.getTierName(); }
        int32                  getBounty() const { return _law.getBounty( _regionID ); }
        int32                  getWantedLevel() const { return _law.getWantedLevel( _regionID ); }
        int32                  countPendingReports() const { return _law.countPendingReports(); }
        int64                  getMoney() const;
        int32                  getCarcassCount() const { return static_cast<int32>( _listCarcass.size() ); }
        int32                  getPeltCount() const { return static_cast<int32>( _listPelt.size() ); }
        float32                computeAirTemperature() const;
        const WesternSurvival& getSurvival() const { return _survival; }
        bool                   isDisguised() const { return _law.isDisguised(); }
        /** @brief 법 쪽에서 생긴 일을 꺼냅니다(로그는 `update` 가 남겼다). 디렉터가 소리로 알린다. */
        void pollLawEvents( vector<WesternLawEvent>& outListEvent ) { _lawEventBuffer.moveAllTo( outListEvent ); }

        /**
         * @brief 법 · 생존 · 사체 · 가죽 · 넘긴 센트 · 전리품 난수 · 지난 `update` 의 시계를 씁니다. 지갑 · 평판 · 시계는 판 상태가 싣는다.
         *        `writeState` 의 바이트로 바꿉니다. 깨졌거나 모르는 동물이면 그대로 두고 false 입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        [[nodiscard]] bool commitCrime( const WesternDeed& deed, const vector<WesternWitness>& listCandidate );
        [[nodiscard]] bool hunt( const WesternDeed& deed );
        [[nodiscard]] bool skinCarcasses();
        [[nodiscard]] bool sellPelts();
        /** @brief 법 쪽에서 생긴 일을 한 줄 로그로 남기고 `pollLawEvents` 가 꺼낼 곳에 둡니다. */
        void logLawEvents();

    private:
        WesternHonor                        _honor;
        WesternLawState                     _law;
        NotificationBuffer<WesternLawEvent> _lawEventBuffer; ///< `pollLawEvents`
        WesternSurvival                     _survival;
        WesternPerceptionSight              _sight;
        GameRandom                          _random;
        PROPERTY( RuntimeState )
        vector<WesternCarcass> _listCarcass;
        PROPERTY( RuntimeState )
        vector<WesternPelt>   _listPelt;
        Wallet*               _pWallet;     ///< 빌린 지갑(`initialize` 뒤 늘 있다)
        ReputationState*      _pReputation; ///< 빌린 평판(`initialize` 뒤 늘 있다)
        WorldClock*           _pClock;      ///< 빌린 시계(`initialize` 뒤 늘 있다)
        GameFlags*            _pFlags;      ///< 빌린 플래그(`initialize` 뒤 늘 있다)
        WeatherSystem*        _pWeather;    ///< 빌린 날씨(`initialize` 뒤 늘 있다)
        Inventory*            _pInventory;  ///< 빌린 가방(전리품을 적은 동물이 없으면 nullptr 일 수 있다)
        hashed_string         _regionID;
        const WesternCatalog* _pCatalog;
        const LootCatalog*    _pLoot;          ///< 빌린 전리품 카탈로그(전리품을 적은 동물이 없으면 nullptr 일 수 있다)
        float64               _lastClockHours; ///< 지난 `update` 의 시계(날 × 24 + 시)
        float32               _regionBaseTemperature;
        PROPERTY( RuntimeState )
        int32 _centsCarry;     ///< 팔고 남은 센트(1 달러 아래)
        uint8 _bClockSeen : 1; ///< `_lastClockHours` 를 잡았다(불러온 뒤에는 다시 잡는다)
    };
} // namespace sw
