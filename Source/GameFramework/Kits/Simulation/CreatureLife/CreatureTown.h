/**
 * @file CreatureTown.h
 * @brief 생물과 함께 만드는 마을 — 칸 격자의 오브젝트, 서식지 맞추기(회전 허용), 시간대 · 날씨에 따른 방문(결정적), 호감도 · 부탁, 능력으로 세계 편집, 집 배정, 매력도입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/GridTopology.h"
#include "GameFramework/Base/World/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureLifeCatalog.h"

namespace sw
{
    struct GameStateRefs;

    class Archive;
    class Inventory;
    class QuestLog;
    class ReputationCatalog;
    class WeatherSystem;

    /** @brief 마을 규칙의 수치입니다. 호감도 점수는 생물마다의 평판(`ReputationState` 의 세력 = 종 id)에 더합니다. */
    struct CreatureTownSettings
    {
        hashed_string _houseObject{ "house" }; ///< 집을 지으면 그 칸에 놓이는 오브젝트
        int32         _talkPoints{ 5 };        ///< 하루 한 번 대화
        int32         _giftPoints{ 10 };       ///< 좋아하지 않는 선물
        int32         _likedGiftPoints{ 30 };  ///< 좋아하는 선물
        int32         _foodPoints{ 20 };       ///< 좋아하는 음식
        int32         _requestPoints{ 50 };    ///< 부탁을 끝냈을 때
        uint32        _randomSeed{ 7100u };    ///< 방문 확률의 씨앗
    };
} // namespace sw

namespace sw
{
    /** @brief 대화 · 선물 결과입니다. */
    enum class CreatureInteractResult : uint8
    {
        Ok = 0,
        UnknownCreature, ///< 마을에 살지 않는다
        AlreadyToday,    ///< 오늘 이미 했다(하루 한 번)
        MissingItem      ///< 줄 아이템이 없다
    };

    /** @brief 부탁 받기 결과입니다. */
    enum class CreatureRequestResult : uint8
    {
        Ok = 0,
        UnknownCreature,
        NotOffered,    ///< 그 생물이 하는 부탁이 아니다
        AlreadyActive, ///< 이미 받았다
        Unavailable    ///< 퀘스트 조건(선행 · 호감도 단계 · 반복)이 맞지 않는다
    };

    /** @brief 능력 쓰기 결과입니다. */
    enum class CreatureAbilityResult : uint8
    {
        Ok = 0,
        UnknownCreature,
        NotKnown,   ///< 그 생물의 능력이 아니다
        NoUsesLeft, ///< 오늘 쓸 수 있는 횟수를 다 썼다
        OutOfBounds,
        NoRule ///< 그 칸의 오브젝트에 맞는 규칙이 없다(물 위에 나무 심기)
    };

    /** @brief 집 배정 결과입니다. */
    enum class CreatureHouseResult : uint8
    {
        Ok = 0,
        UnknownCreature,
        UnknownHouse,
        HouseFull
    };

    SW_GF_API const utf8* toString( CreatureInteractResult result );
    SW_GF_API const utf8* toString( CreatureRequestResult result );
    SW_GF_API const utf8* toString( CreatureAbilityResult result );
    SW_GF_API const utf8* toString( CreatureHouseResult result );

    /** @brief 격자 위에 생긴 서식지 하나입니다. */
    struct HabitatInstance
    {
        hashed_string _habitatId{};
        vector<int32> _listTile{};    ///< 차지한 칸(y × width + x)
        int2          _origin{};      ///< 회전한 패턴의 왼쪽 아래 칸
        int32         _id{ 0 };       ///< 사라질 때까지 같은 번호(생물이 가리킨다)
        int32         _rotation{ 0 }; ///< 0..3 — 90° 단위
    };
} // namespace sw

namespace sw
{
    /** @brief 마을에 사는 생물 하나입니다. */
    struct TownCreature
    {
        hashed_string _speciesId{};
        vector<int32> _listAbilityUse{}; ///< 종의 `_listAbility` 와 같은 자리 — 오늘 쓴 횟수
        int32         _habitat{ -1 };    ///< 머무는 서식지 번호(`HabitatInstance::_id`, 사라졌으면 −1)
        int32         _house{ -1 };      ///< 사는 집 자리(없으면 −1)
        int32         _lastTalkDay{ -1 };
        int32         _lastGiftDay{ -1 };
        int32         _arrivalDay{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 생물이 사는 집 하나입니다. */
    struct CreatureHouse
    {
        vector<hashed_string> _listResident{}; ///< 사는 생물(종 id)
        int2                  _tile{};
        int32                 _capacity{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 마을에 생긴 일입니다. */
    struct CreatureTownEvent
    {
        enum class Kind : uint8
        {
            HabitatFormed = 0, ///< _id = 서식지 id, _value = 서식지 번호
            HabitatLost,
            CreatureArrived,       ///< _id = 종, _value = 서식지 번호
            FriendshipTierChanged, ///< _id = 종, _detail = 새 단계, _value = 호감도
            RequestCompleted,      ///< _id = 종, _detail = 퀘스트
            AbilityUsed,           ///< _id = 종, _detail = 얻은 아이템(없으면 빔), _value = 칸
            AppealTierChanged      ///< _detail = 새 단계, _value = 단계 번호
        };
        hashed_string _id{};
        hashed_string _detail{};
        int32         _value{ 0 };
        Kind          _kind{ Kind::HabitatFormed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CreatureTown
     * @brief 포코피아 · 문스톤 아일랜드 류의 생활 규칙입니다.
     * @details 서식지 — 칸이 바뀔 때마다 처음부터 다시 맞춥니다. 차지 칸이 많은 레시피부터, 아래 줄 → 위 줄, 왼쪽 → 오른쪽, 회전 0 → 3 순서로 맞추고
     *          이미 다른 서식지가 차지한 칸은 쓰지 않습니다(결정적). 전과 같은 레시피 · 자리 · 회전이면 같은 서식지(번호 · 사는 생물 유지)이고,
     *          없어진 서식지의 생물은 좋아하는 다른 서식지에 자리가 있으면 옮겨 갑니다.
     *          방문 — `attractVisitors` 를 시마다 부르면 자리가 남은 서식지마다, 그 서식지를 좋아하고 지금 때 · 날씨에 오는 종(아직 마을에 없는)이
     *          `chance` 확률로 찾아옵니다. 확률은 (날 · 시 · 종 · 서식지 자리 · 씨앗) 해시라 같은 시를 두 번 불러도 같은 답입니다(두 번째는 아무 일 없음).
     *          호감도 — 기반 `ReputationState` 의 세력 하나가 생물 하나입니다. 대화 · 선물은 각각 하루 한 번, 부탁(기반 `QuestLog`)을 끝내면 오릅니다.
     *          부탁 일지 · 시계는 빌려 씁니다 — 마을은 일지 알림을 꺼내지 않고 받은 부탁의 상태만 봅니다(알림은 게임 화면 · 다른 키트의 것). 날은 빌린 시계의 날입니다.
     *          부탁 목표는 `Deliver`(아이템) · `Habitat`(그 서식지 수)이고 퀘스트 레벨 조건은 호감도 단계 번호로 봅니다.
     *          카탈로그는 빌려 씁니다(마을보다 오래 살아야 합니다).
     */
    class SW_GF_API CreatureTown
    {
    public:
        static constexpr uint32 kStateTag     = 0x4E575443u; ///< 'CTWN'
        static constexpr uint32 kStateVersion = 1;

        CreatureTown();

        /**
         * @brief 크기를 정하고 모든 칸을 비웁니다. 평판 카탈로그 · 부탁 일지 · 시계는 없어도 됩니다(평판은 0..1000 단계 없음, 일지가 없으면 부탁 없음, 시계가 없으면 0 일).
         * @param refs 빌려 쓰는 공유 상태입니다 — 마을은 `_pQuestLog`(부탁 일지) · `_pClock`(날)을 쓴다(마을보다 오래 살아야 한다). 섞인 게임은 `GameStateComponent::makeRefs()`.
         */
        void initialize( const CreatureLifeCatalog* pCatalog, const ReputationCatalog* pReputationCatalog, const GameStateRefs& refs, int32 width, int32 height,
                         const CreatureTownSettings& settings );

        /** @brief 칸에 오브젝트를 놓습니다(빈 id 는 비우기). 서식지를 다시 맞춥니다. 밖이면 false 입니다. */
        bool setObject( int32 x, int32 y, const hashed_string& object );
        /** @brief 시계 · 날씨로 이번 시의 방문을 굴립니다. 찾아온 수입니다. */
        int32 attractVisitors( const WorldClock& clock, const WeatherSystem& weather );
        /** @brief 날 · 시 · 때 · 날씨를 직접 줍니다(시험 · 서버). */
        int32 attractVisitorsAt( int32 day, int32 hour, DayPhase phase, const hashed_string& weatherId );

        CreatureInteractResult talkTo( const hashed_string& speciesId );
        /** @brief @p inventory 에서 하나를 빼서 줍니다 — 좋아하는 선물 · 음식 · 그 밖으로 점수가 다릅니다. */
        CreatureInteractResult giveGift( const hashed_string& speciesId, const hashed_string& itemId, Inventory& inventory );
        CreatureRequestResult  startRequest( const hashed_string& speciesId, const hashed_string& questId );
        /**
         * @brief 부탁의 `Deliver` 목표에 아이템을 건넵니다. 목표가 그것을 세었을 때만 @p inventory 에서 뺍니다.
         * @return 진행한 목표 수(0 이면 아무것도 빼지 않았다).
         */
        int32 deliverItem( const hashed_string& itemId, int32 count, Inventory& inventory );
        /** @brief 능력으로 칸을 바꿉니다. 얻은 아이템은 @p pYieldInventory(없어도 된다)에 넣습니다. */
        CreatureAbilityResult useAbility( const hashed_string& speciesId, const hashed_string& abilityId, int32 x, int32 y, Inventory* pYieldInventory );

        /** @brief 빈 칸에 집을 짓습니다. 집 자리 번호, 밖이거나 칸이 차 있으면 −1 입니다. */
        int32               placeHouse( int32 x, int32 y, int32 capacity );
        CreatureHouseResult assignHouse( const hashed_string& speciesId, int32 houseIndex );
        /** @brief 집 없는 생물을 온 순서대로 자리가 남은 가장 가까운 집(서식지 기준 맨해튼 거리, 같으면 앞 집)에 넣습니다. 넣은 수입니다. */
        int32 assignHomeless();

        /** @brief 하루를 넘깁니다 — 능력 횟수를 되돌리고 호감도가 식습니다(대화 · 선물은 시계의 날로 하루 한 번). 디렉터가 시계의 날 넘김에 부릅니다. */
        void advanceDay();
        void drainEvents( vector<CreatureTownEvent>& outListEvent );
        /** @brief 칸 · 서식지(번호 포함) · 생물 · 집 · 받은 부탁 · 호감도를 씁니다(핫 리로드 · 세이브). 부탁 일지 · 시계는 빌린 것이라 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 마을 크기가 다르거나 깨졌으면 false 이고 그대로입니다(카탈로그 · 일지 · 시계는 `initialize` 의 것). */
        [[nodiscard]] bool readState( Archive& archive );

        const hashed_string*           findObject( int32 x, int32 y ) const;
        const TownCreature*            findCreature( const hashed_string& speciesId ) const;
        const HabitatInstance*         findHabitat( int32 habitatInstanceId ) const;
        const vector<TownCreature>&    getCreatures() const { return _listCreature; }
        const vector<HabitatInstance>& getHabitats() const { return _listHabitat; }
        const vector<CreatureHouse>&   getHouses() const { return _listHouse; }
        /** @brief 그 레시피의 서식지 수입니다. */
        int32 countHabitats( const hashed_string& habitatId ) const;
        /** @brief 서식지에 머무는 생물 수입니다. */
        int32 countResidents( int32 habitatInstanceId ) const;
        /** @brief 오늘 남은 능력 횟수입니다. 모르면 0 입니다. */
        int32         countAbilityUsesLeft( const hashed_string& speciesId, const hashed_string& abilityId ) const;
        int32         getFriendship( const hashed_string& speciesId ) const { return _friendship.getValue( speciesId ); }
        hashed_string getFriendshipTier( const hashed_string& speciesId ) const { return _friendship.getTierName( speciesId ); }
        /** @brief 사는 생물의 호감도 평균입니다(없으면 0). */
        float32 computeAverageFriendship() const;
        /** @brief 매력도 점수입니다(서식지 종류 · 생물 수 · 호감도 평균). */
        float32 computeAppealScore() const;
        /** @brief 매력도 단계 번호입니다(단계가 없으면 −1). */
        int32         getAppealTierIndex() const { return _appealTier; }
        hashed_string getAppealTierName() const;
        /** @brief 빌린 시계의 날입니다(없으면 0). */
        int32 getDay() const;
        int32 getWidth() const { return _topology._width; }
        int32 getHeight() const { return _topology._height; }

    private:
        /** @brief 처음부터 다시 맞추고 전 결과와 견줘 생긴 · 없어진 서식지를 알립니다. */
        void refreshHabitats();
        /** @brief 패턴을 그 자리 · 회전에 맞춰 봅니다. 맞으면 차지할 칸을 @p outListTile 에 적습니다. */
        bool  matchesAt( const HabitatDef& habitat, int32 rotation, int32 originX, int32 originY, const vector<uint8>& listClaimed,
                         vector<int32>& outListTile ) const;
        int32 findCreatureIndex( const hashed_string& speciesId ) const;
        bool  hasRoom( const HabitatInstance& instance ) const;
        void  notifyHabitatObjectives();
        void  flushReputationEvents();
        /** @brief 받은 부탁 중 끝난 것에 보상하고 목록에서 뺍니다(실패 · 포기는 보상 없이). 일지 알림은 꺼내지 않는다. */
        void collectCompletedRequests();
        /** @brief 부탁 @p questId 를 하는(마을에 사는) 생물에게 호감도를 줍니다. 준 생물이 있으면 true 입니다. */
        bool rewardRequest( const hashed_string& questId );
        void updateAppealTier();
        int2 computeCreatureAnchor( const TownCreature& creature ) const;

        vector<hashed_string>          _listObject; ///< 칸마다 오브젝트 id(`_topology` 의 칸 번호) — 빈 id 는 빈 칸
        vector<HabitatInstance>        _listHabitat;
        vector<TownCreature>           _listCreature; ///< 온 순서
        vector<CreatureHouse>          _listHouse;
        vector<hashed_string>          _listOpenRequest; ///< 이 마을이 받은 부탁 중 아직 끝나지 않은 것(퀘스트 id)
        EventBuffer<CreatureTownEvent> _eventBuffer;
        vector<ReputationEvent>        _listReputationScratch;
        ReputationState                _friendship;
        CreatureTownSettings           _settings;
        const CreatureLifeCatalog*     _pCatalog;
        QuestLog*                      _pQuestLog; ///< 빌린 부탁 일지(없으면 부탁 없음)
        const WorldClock*              _pClock;    ///< 빌린 시계(날 — 없으면 0 일)
        GridTopology                   _topology;
        int32                          _nextHabitatId;
        int32                          _lastAttractKey; ///< 마지막으로 방문을 굴린 날 × 24 + 시
        int32                          _appealTier;
    };
} // namespace sw
