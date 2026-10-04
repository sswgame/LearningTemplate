/**
 * @file ScavengerCatalog.h
 * @brief 협동 수집 공포(리썰 컴퍼니 장르)의 데이터 — 할당량 주기 · 매입률 · 하루 시각 · 운반 · 벌금 · 시설 생성 · 고철 · 위성(달)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 할당량 주기의 규칙입니다. */
    struct ScavengerQuotaSettings
    {
        float32 _increase{ 100.0f };      ///< 다음 할당량에 더하는 기본 양
        float32 _steepness{ 16.0f };      ///< 늘어남 = 기본 × (1 + 주기² / 이 값) — 주기가 갈수록 가파르다
        float32 _randomness{ 0.5f };      ///< 늘어남에 곱하는 (1 + 이 값 × (r − 0.5)), r ∈ [0, 1)
        float32 _overtimeDivisor{ 5.0f }; ///< 넘긴 양 / 이 값 = 마감 보너스(0 = 없음)
        int32   _startQuota{ 130 };
        int32   _daysPerCycle{ 3 }; ///< 마감 전 남은 날(이것이 0 인 날이 마감 날)
        int32   _startCredits{ 60 };
    };
} // namespace sw

namespace sw
{
    /** @brief 남은 날 → 매입률 점 하나입니다(사이는 선형). */
    struct ScavengerBuyRatePoint
    {
        int32   _daysLeft{ 0 };
        float32 _rate{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 하루 시각입니다(0..24 시). */
    struct ScavengerDaySettings
    {
        float32 _secondsPerDay{ 720.0f }; ///< 실제 초로 하루 24 시간
        float32 _arrivalHour{ 7.0f };     ///< 내리는 시각
        float32 _duskHour{ 18.0f };       ///< 해 질 녘 알림
        float32 _departHour{ 24.0f };     ///< 이 시각(자정)에 우주선이 저절로 떠난다 — 밖에 남은 사람은 죽는다
    };
} // namespace sw

namespace sw
{
    /** @brief 들고 다니기 규칙입니다. */
    struct ScavengerCarrySettings
    {
        float32 _speedPerWeight{ 0.004f }; ///< 무게 1 마다 줄어드는 이동 속도 몫
        float32 _minSpeedScale{ 0.4f };
        float32 _bodyWeight{ 60.0f }; ///< 동료 시신 무게(양손)
        int32   _slotCount{ 4 };
    };
} // namespace sw

namespace sw
{
    /** @brief 죽음 · 전멸 규칙입니다. */
    struct ScavengerPenaltySettings
    {
        float32 _deathFine{ 0.2f };        ///< 시신을 못 가져온 사람마다 크레딧의 이 몫
        float32 _recoveredFine{ 0.05f };   ///< 시신을 가져온 사람마다
        float32 _allDeadLossRatio{ 1.0f }; ///< 모두 죽으면 우주선 고철의 이 몫을 잃는다
        int32   _allDeadMaxKept{ 0 };      ///< 그래도 이만큼은 남긴다(0 = 몫 그대로)
    };
} // namespace sw

namespace sw
{
    /** @brief 시설(실내) 생성 규칙입니다. */
    struct ScavengerFacilitySettings
    {
        float32 _lockedChance{ 0.2f }; ///< 방으로 들어가는 문이 잠겨 있을 확률
        float32 _loopChance{ 0.15f };  ///< 나무에 더하는 고리 통로 확률(방마다)
        int32   _minRooms{ 8 };
        int32   _maxRooms{ 14 };
        int32   _fireExits{ 1 }; ///< 바깥으로 이어진 화재 출구 수
    };
} // namespace sw

namespace sw
{
    /** @brief 고철 한 종류입니다. */
    struct ScavengerScrapDef
    {
        hashed_string _id{};
        float32       _weight{ 10.0f };
        float32       _spawnWeight{ 1.0f };
        int32         _minValue{ 20 };
        int32         _maxValue{ 60 };
        uint8         _bTwoHanded{ SW_FALSE }; ///< 양손 — 드는 동안 다른 것을 줍지 못한다
    };
} // namespace sw

namespace sw
{
    /** @brief 위성(달) 하나입니다. */
    struct ScavengerMoonDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listScrap{};           ///< 나오는 고철(비면 모두)
        float32               _risk{ 1.0f };          ///< 위협 예산이 쌓이는 배율
        float32               _minValueScale{ 1.0f }; ///< 고철 가치 배율 범위
        float32               _maxValueScale{ 1.0f };
        int32                 _routeCost{ 0 }; ///< 가는 데 드는 크레딧
        int32                 _minScrap{ 8 };
        int32                 _maxScrap{ 12 };
        uint8                 _bCompany{ SW_FALSE }; ///< 회사 — 시설 · 위협이 없고 고철을 판다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScavengerCatalog
     * @brief `<ScavengerCatalog terminalShop="terminal" currency="Credits" crewHealth="100"><Quota start="130" days="3" increase="100" steepness="16" randomness="0.5" overtime="5" credits="60"/>
     *        <BuyRate daysLeft="3" rate="0.3"/><Day secondsPerDay="720" arrival="7" dusk="18" depart="24"/><Carry slots="4" speedPerWeight="0.004" minSpeed="0.4" bodyWeight="60"/>
     *        <Penalty deathFine="0.2" recoveredFine="0.05" allDeadLoss="1" allDeadKeep="0"/><Facility rooms="8" roomsMax="14" lockedChance="0.2" loopChance="0.15" fireExits="1"/>
     *        <Scrap id="bolt" min="20" max="40" weight="5" spawnWeight="5" twoHanded="false"/>
     *        <Moon id="experimentation" risk="1" cost="0" scrap="8" scrapMax="12" valueMin="0.9" valueMax="1.1" scraps="bolt,bell" company="false"/></ScavengerCatalog>` 를 읽습니다.
     */
    class SW_GF_API ScavengerCatalog : public XmlCatalog<ScavengerCatalog>
    {
        friend class XmlCatalog<ScavengerCatalog>;

    public:
        ScavengerCatalog();

        const ScavengerMoonDef*          findMoon( const hashed_string& id ) const { return _moonCatalog.find( id ); }
        const vector<ScavengerMoonDef>&  getMoons() const { return _moonCatalog.getAll(); }
        const ScavengerScrapDef*         findScrap( const hashed_string& id ) const { return _scrapCatalog.find( id ); }
        const vector<ScavengerScrapDef>& getScraps() const { return _scrapCatalog.getAll(); }
        /** @brief 마감까지 @p daysLeft 일 남았을 때의 매입률입니다(점 사이는 선형, 끝 밖은 끝 값, 점이 없으면 1). */
        float32                          computeBuyRate( int32 daysLeft ) const;
        const ScavengerQuotaSettings&    getQuotaSettings() const { return _quota; }
        const ScavengerDaySettings&      getDaySettings() const { return _day; }
        const ScavengerCarrySettings&    getCarrySettings() const { return _carry; }
        const ScavengerPenaltySettings&  getPenaltySettings() const { return _penalty; }
        const ScavengerFacilitySettings& getFacilitySettings() const { return _facility; }
        const hashed_string&             getTerminalShopId() const { return _terminalShopId; }
        const hashed_string&             getCurrency() const { return _currency; }
        float32                          getCrewHealth() const { return _crewHealth; }

    private:
        static constexpr const utf8* kXmlRootName = "ScavengerCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<ScavengerMoonDef>  _moonCatalog;
        GameCatalog<ScavengerScrapDef> _scrapCatalog;
        vector<ScavengerBuyRatePoint>  _listBuyRate; ///< 남은 날 오름차순
        ScavengerQuotaSettings         _quota;
        ScavengerDaySettings           _day;
        ScavengerCarrySettings         _carry;
        ScavengerPenaltySettings       _penalty;
        ScavengerFacilitySettings      _facility;
        hashed_string                  _terminalShopId;
        hashed_string                  _currency;
        float32                        _crewHealth; ///< 사람 하나의 체력
    };
} // namespace sw
