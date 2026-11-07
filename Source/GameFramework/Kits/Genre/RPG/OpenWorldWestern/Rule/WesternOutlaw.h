/**
 * @file WesternOutlaw.h
 * @brief 플레이어 한 명의 서부극 삶입니다. 명예(`WesternHonor`), 법(`WesternLawState`), 생존(`WesternSurvival`), 사냥한 사체와 가죽을 한 지역에 묶어 시계로 흘리고, 행동(`WesternDeed`) 하나를 규칙들에 나눠 줍니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHonor.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHunting.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternLaw.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternSurvival.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    class Archive;

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
     * @brief 게임 디렉터가 하나 가지는 보통 클래스입니다(세이브는 디렉터가 `writeState` 로 싣는다). 지갑 · 평판 · 시계 · 날씨 · 플래그는 빌려 씁니다(`SessionStateRefs`).
     * @details 시간은 빌린 시계로 잰다 — `update` 가 지난번 뒤로 흐른 게임 시간과 넘은 날을 구해 생존 · 사체 · 현상금에 넘긴다. 잠과 빨리 감기(`AdvanceGameTime`)도 같은 길로 닿는다.
     *          돈의 단위는 카탈로그의 통화 하나다. 가죽 값(`WesternHunting`)은 센트이므로 팔 때 달러로 내리고 남은 센트는 다음 판매로 넘긴다. 명예 단계의 할인 비율은 파는 값에 웃돈으로 얹는다.
     *          레드 데드 리뎀션 2 의 명예 · 현상금 · 코어 흐름을 따랐다(기억에 의존한 비교).
     */
    class SW_KIT_API WesternOutlaw
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WOTL" );
        static constexpr uint32 kStateVersion = 1;

        WesternOutlaw();

        /**
         * @brief 지역 @p regionID 에서 시작합니다. @p regionBaseTemperature 는 날씨 값을 더하기 전의 기온(섭씨)이고 @p sight 는 목격자의 시야입니다.
         * @return 카탈로그 · 지갑 · 평판 · 시계가 없거나, 지역이 카탈로그에 없거나, 생존 수치를 열지 못하면 오류를 남기고 false 입니다(시작하지 않는다).
         */
        [[nodiscard]] bool initialize( const WesternCatalog* pCatalog, const SessionStateRefs& refs, const hashed_string& regionID, float32 regionBaseTemperature, const AIPerceptionSettings& sight );
        /** @brief 실제 @p deltaTime 초와 빌린 시계가 지난번 뒤로 흐른 만큼 흘립니다. 대사 플래그를 맞춥니다. */
        void update( float32 deltaTime );
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

        /**
         * @brief 법 · 생존 · 사체 · 가죽 · 넘긴 센트를 씁니다. 지갑과 평판은 판 상태가 싣는다.
         *        `writeState` 의 바이트로 바꿉니다. 깨졌거나 모르는 동물이면 그대로 두고 false 입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        [[nodiscard]] bool commitCrime( const WesternDeed& deed, const vector<WesternWitness>& listCandidate );
        [[nodiscard]] bool hunt( const WesternDeed& deed );
        [[nodiscard]] bool skinCarcasses();
        [[nodiscard]] bool sellPelts();
        /** @brief 법 쪽에서 생긴 일을 한 줄 로그로 남깁니다. */
        void logLawEvents();

    private:
        WesternHonor           _honor;
        WesternLawState        _law;
        WesternSurvival        _survival;
        WesternPerceptionSight _sight;
        GameRandom             _random;
        vector<WesternCarcass> _listCarcass;
        vector<WesternPelt>    _listPelt;
        SessionStateRefs       _refs;
        hashed_string          _regionID;
        const WesternCatalog*  _pCatalog;
        float64                _lastClockHours; ///< 지난 `update` 의 시계(날 × 24 + 시)
        float32                _regionBaseTemperature;
        int32                  _centsCarry;     ///< 팔고 남은 센트(1 달러 아래)
        uint8                  _bClockSeen : 1; ///< `_lastClockHours` 를 잡았다(불러온 뒤에는 다시 잡는다)
        uint8                  _reserved   : 7;
    };
} // namespace sw
