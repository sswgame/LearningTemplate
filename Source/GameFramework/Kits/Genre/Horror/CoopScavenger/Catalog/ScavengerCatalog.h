/**
 * @file ScavengerCatalog.h
 * @brief 협동 수집 공포(리썰 컴퍼니 장르)의 데이터를 담습니다. 할당량 주기, 매입률, 하루 시각, 운반, 벌금, 시설 생성, 고철, 위성(달)을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 할당량 주기의 규칙입니다. */
    REFLECT()
    struct ScavengerQuotaDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0 )
        float32 _increase{ 100.0f }; ///< 다음 할당량에 더하는 기본 양
        PROPERTY( Min = 0.01 )
        float32 _steepness{ 16.0f }; ///< 늘어나는 양은 기본 x (1 + 주기^2 / 이 값)이다. 주기가 갈수록 가파르게 오른다
        PROPERTY( Min = 0.0, Max = 2.0 )
        float32 _randomness{ 0.5f }; ///< 늘어나는 양에 곱하는 (1 + 이 값 x (r - 0.5))이다. r 은 [0, 1) 범위의 난수
        PROPERTY( Name = "overtime", Min = 0.0 )
        float32 _overtimeDivisor{ 5.0f }; ///< 넘긴 양 / 이 값이 마감 보너스이다(0 이면 보너스 없음)
        PROPERTY( Name = "start", Min = 1 )
        int32 _startQuota{ 130 };
        PROPERTY( Name = "days", Min = 0 )
        int32 _daysPerCycle{ 3 }; ///< 마감 전 남은 날이다(이 값이 0 인 날이 마감 날)
        PROPERTY( Name = "credits", Min = 0 )
        int32 _startCredits{ 60 };
    };
} // namespace sw

namespace sw
{
    /** @brief 남은 날에 대한 매입률 점 하나입니다. 점 사이는 선형으로 보간합니다. */
    REFLECT()
    struct ScavengerBuyRatePoint
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0 )
        int32 _daysLeft{ 0 };
        PROPERTY( Required, Min = 0.0 )
        float32 _rate{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 하루 시각입니다(0..24시). */
    REFLECT()
    struct ScavengerDayDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0 )
        float32 _secondsPerDay{ 720.0f }; ///< 실제 초로 잰 하루 24시간의 길이
        PROPERTY( Name = "arrival", Min = 0.0, Max = 23.0 )
        float32 _arrivalHour{ 7.0f }; ///< 위성에 내리는 시각
        PROPERTY( Name = "dusk", Min = "_arrivalHour", Max = 24.0 )
        float32 _duskHour{ 18.0f }; ///< 해 질 녘 알림 시각
        PROPERTY( Name = "depart", Min = "_duskHour", Max = 48.0 )
        float32 _departHour{ 24.0f }; ///< 이 시각(자정)에 우주선이 저절로 떠난다. 밖에 남은 사람은 죽는다
    };
} // namespace sw

namespace sw
{
    /** @brief 들고 다니기 규칙입니다. */
    REFLECT()
    struct ScavengerCarryDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0 )
        float32 _speedPerWeight{ 0.004f }; ///< 무게 1 마다 줄어드는 이동 속도 비율
        PROPERTY( Name = "minSpeed", Min = 0.0, Max = 1.0 )
        float32 _minSpeedScale{ 0.4f };
        PROPERTY( Min = 0.0 )
        float32 _bodyWeight{ 60.0f }; ///< 동료 시신의 무게이다(양손)
        PROPERTY( Name = "slots", Min = 1, Max = 16 )
        int32 _slotCount{ 4 };
    };
} // namespace sw

namespace sw
{
    /** @brief 죽음과 전멸 규칙입니다. */
    REFLECT()
    struct ScavengerPenaltyDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _deathFine{ 0.2f }; ///< 시신을 못 가져온 사람마다 크레딧에서 이 비율을 벌금으로 낸다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _recoveredFine{ 0.05f }; ///< 시신을 가져온 사람마다 내는 벌금 비율
        PROPERTY( Name = "allDeadLoss", Min = 0.0, Max = 1.0 )
        float32 _allDeadLossRatio{ 1.0f }; ///< 모두 죽으면 우주선 고철의 이 비율을 잃는다
        PROPERTY( Name = "allDeadKeep", Min = 0 )
        int32 _allDeadMaxKept{ 0 }; ///< 그래도 최소한 이만큼은 남긴다(0 이면 비율 그대로 잃는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 시설(실내) 생성 규칙입니다. */
    REFLECT()
    struct ScavengerFacilityDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _lockedChance{ 0.2f }; ///< 방으로 들어가는 문이 잠겨 있을 확률
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _loopChance{ 0.15f }; ///< 나무 구조에 더하는 고리 통로의 확률이다(방마다 적용)
        PROPERTY( Name = "rooms", Min = 1, Max = 256 )
        int32 _minRooms{ 8 };
        PROPERTY( Name = "roomsMax", Min = "_minRooms", Max = 256 )
        int32 _maxRooms{ 14 };
        PROPERTY( Min = 0 )
        int32 _fireExits{ 1 }; ///< 바깥으로 이어진 화재 출구 수
    };
} // namespace sw

namespace sw
{
    /** @brief 고철 한 종류입니다. */
    REFLECT()
    struct ScavengerScrapDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 10.0f };
        PROPERTY( Min = 0.0 )
        float32 _spawnWeight{ 1.0f };
        PROPERTY( Name = "min", Min = 0 )
        int32 _minValue{ 20 };
        PROPERTY( Name = "max", Required, Min = "_minValue" )
        int32 _maxValue{ 60 };
        PROPERTY()
        bool _bTwoHanded{ false }; ///< 양손 아이템이다. 드는 동안 다른 것을 줍지 못한다
    };
} // namespace sw

namespace sw
{
    /** @brief 위성(달) 하나입니다. */
    REFLECT()
    struct ScavengerMoonDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string                _name{};
        vector<hashed_string> _listScrap{}; ///< 나오는 고철이다(비어 있으면 모두)
        PROPERTY( Min = 0.0 )
        float32 _risk{ 1.0f }; ///< 위협 예산이 쌓이는 배율
        PROPERTY( Name = "valueMin", Min = 0.0 )
        float32 _minValueScale{ 1.0f }; ///< 고철 가치 배율 범위이다
        PROPERTY( Name = "valueMax", Min = "_minValueScale" )
        float32 _maxValueScale{ 1.0f };
        PROPERTY( Name = "cost", Min = 0 )
        int32 _routeCost{ 0 }; ///< 가는 데 드는 크레딧
        PROPERTY( Name = "scrap", Min = 0 )
        int32 _minScrap{ 8 };
        PROPERTY( Name = "scrapMax", Min = "_minScrap" )
        int32 _maxScrap{ 12 };
        PROPERTY()
        bool _bCompany{ false }; ///< 회사 위성이다. 시설과 위협이 없고 고철을 판다
    };
} // namespace sw

namespace sw
{
    /** @brief `<ScavengerCatalog>` 루트 속성의 스키마입니다. */
    REFLECT()
    struct ScavengerRootDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "terminalShop" )
        hashed_string _terminalShopID{ "terminal" }; ///< 터미널 상점 id
        PROPERTY()
        hashed_string _currency{ "Credits" };
        PROPERTY( Min = 1.0 )
        float32 _crewHealth{ 100.0f };
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
     *        <Moon id="experimentation" name="Experimentation" risk="1" cost="0" scrap="8" scrapMax="12" valueMin="0.9" valueMax="1.1" scraps="bolt,bell" company="false"/></ScavengerCatalog>` 를 읽습니다.
     */
    class SW_KIT_API ScavengerCatalog : public XMLCatalog<ScavengerCatalog>
    {
        friend class XMLCatalog<ScavengerCatalog>;

    public:
        ScavengerCatalog();

        const ScavengerMoonDef*          findMoon( const hashed_string& id ) const { return _moonCatalog.find( id ); }
        const vector<ScavengerMoonDef>&  getMoons() const { return _moonCatalog.getAll(); }
        const ScavengerScrapDef*         findScrap( const hashed_string& id ) const { return _scrapCatalog.find( id ); }
        const vector<ScavengerScrapDef>& getScraps() const { return _scrapCatalog.getAll(); }
        /** @brief 마감까지 @p daysLeft 일 남았을 때의 매입률을 계산합니다. 점 사이는 선형으로 보간하고, 끝 밖은 끝 값을 쓰며, 점이 없으면 1 입니다. */
        float32                     computeBuyRate( int32 daysLeft ) const;
        const ScavengerQuotaDef&    getQuotaSettings() const { return _quota; }
        const ScavengerDayDef&      getDaySettings() const { return _day; }
        const ScavengerCarryDef&    getCarrySettings() const { return _carry; }
        const ScavengerPenaltyDef&  getPenaltySettings() const { return _penalty; }
        const ScavengerFacilityDef& getFacilitySettings() const { return _facility; }
        const hashed_string&        getTerminalShopID() const { return _terminalShopID; }
        const hashed_string&        getCurrency() const { return _currency; }
        float32                     getCrewHealth() const { return _crewHealth; }

    private:
        static constexpr const utf8* kXMLRootName = "ScavengerCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 이름에 맞는 스키마로 읽습니다. 오류면 그 원소를 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool loadMoon( const XMLNode& node, string_view sourceName );

        GameCatalog<ScavengerMoonDef>  _moonCatalog;
        GameCatalog<ScavengerScrapDef> _scrapCatalog;
        vector<ScavengerBuyRatePoint>  _listBuyRate; ///< 남은 날 오름차순이다
        ScavengerQuotaDef              _quota;
        ScavengerDayDef                _day;
        ScavengerCarryDef              _carry;
        ScavengerPenaltyDef            _penalty;
        ScavengerFacilityDef           _facility;
        hashed_string                  _terminalShopID;
        hashed_string                  _currency;
        float32                        _crewHealth; ///< 사람 하나의 체력
    };
} // namespace sw
