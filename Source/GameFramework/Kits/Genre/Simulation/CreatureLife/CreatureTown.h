/**
 * @file CreatureTown.h
 * @brief 생물과 함께 만드는 마을을 구현합니다. 셀 격자의 오브젝트, 서식지 맞추기(회전 허용), 시간대와 날씨에 따른 방문(결정적), 호감도와 부탁, 능력으로 세계 편집, 집 배정, 매력도를 다룹니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct HabitatDef;
    struct SessionStateRefs;

    class Archive;
    class CreatureLifeCatalog;
    class Inventory;
    class QuestLog;
    class ReputationState;
    class WeatherSystem;

    /** @brief 마을 규칙의 수치입니다. 호감도 점수는 생물마다의 평판(`ReputationState` 의 세력이며 종 id 와 같습니다)에 더합니다. */
    struct CreatureTownSettings
    {
        hashed_string _houseObject{ "house" }; ///< 집을 지으면 그 셀에 놓이는 오브젝트
        int32         _talkPoints{ 5 };        ///< 하루 한 번 대화할 때의 점수
        int32         _giftPoints{ 10 };       ///< 좋아하지 않는 선물의 점수
        int32         _likedGiftPoints{ 30 };  ///< 좋아하는 선물의 점수
        int32         _foodPoints{ 20 };       ///< 좋아하는 음식의 점수
        int32         _requestPoints{ 50 };    ///< 부탁을 끝냈을 때의 점수
        uint32        _randomSeed{ 7100u };    ///< 방문 확률에 쓰는 시드
        /** @brief 공유 땅에서 막힘으로 얻는 오브젝트입니다(다른 키트의 길찾기가 피합니다). 나머지(풀, 꽃)는 막힘 없이 얻습니다. */
        vector<hashed_string> _listBlockingObject{ hashed_string( "house" ), hashed_string( "tree" ), hashed_string( "rock" ) };
    };
} // namespace sw

namespace sw
{
    /** @brief 대화와 선물의 결과입니다. */
    ENUM()
    enum class CreatureInteractResult : uint8
    {
        Ok = 0,
        UnknownCreature, ///< 마을에 살지 않는다
        AlreadyToday,    ///< 오늘 이미 했다(하루 한 번)
        MissingItem      ///< 줄 아이템이 없다
    };

    /** @brief 부탁 받기의 결과입니다. */
    ENUM()
    enum class CreatureRequestResult : uint8
    {
        Ok = 0,
        UnknownCreature,
        NotOffered,    ///< 그 생물이 하는 부탁이 아니다
        AlreadyActive, ///< 이미 받았다
        Unavailable    ///< 퀘스트 조건(선행, 호감도 단계, 반복)이 맞지 않는다
    };

    /** @brief 능력 쓰기의 결과입니다. */
    ENUM()
    enum class CreatureAbilityResult : uint8
    {
        Ok = 0,
        UnknownCreature,
        NotKnown,   ///< 그 생물의 능력이 아니다
        NoUsesLeft, ///< 오늘 쓸 수 있는 횟수를 다 썼다
        OutOfBounds,
        NoRule,   ///< 그 셀의 오브젝트에 맞는 규칙이 없다(물 위에 나무 심기)
        LandTaken ///< 그 셀의 공유 땅을 다른 키트가 쓴다
    };

    /** @brief 집 배정의 결과입니다. */
    ENUM()
    enum class CreatureHouseResult : uint8
    {
        Ok = 0,
        UnknownCreature,
        UnknownHouse,
        HouseFull
    };

    /** @brief 격자 위에 생긴 서식지 하나입니다. */
    struct HabitatInstance
    {
        hashed_string _habitatID{};
        vector<int32> _listTile{};    ///< 차지한 타일이다(y x width + x)
        int2          _origin{};      ///< 회전한 패턴의 왼쪽 아래 셀
        int32         _id{ 0 };       ///< 사라질 때까지 같은 번호이다(생물이 가리킨다)
        int32         _rotation{ 0 }; ///< 0..3 범위이며 90° 단위이다
    };
} // namespace sw

namespace sw
{
    /** @brief 마을에 사는 생물 하나입니다. */
    struct TownCreature
    {
        hashed_string _speciesID{};
        vector<int32> _listAbilityUse{}; ///< 종의 `_listAbility` 와 같은 순서이며 오늘 쓴 횟수이다
        int32         _habitat{ -1 };    ///< 머무는 서식지 번호이다(`HabitatInstance::_id` 이며 사라졌으면 -1)
        int32         _house{ -1 };      ///< 사는 집의 index 이다(없으면 -1)
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
        vector<hashed_string> _listResident{}; ///< 사는 생물이다(종 id)
        int2                  _tile{};
        int32                 _capacity{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 마을에서 일어난 일을 알리는 이벤트입니다. */
    struct CreatureTownEvent
    {
        enum class Kind : uint8
        {
            HabitatFormed = 0, ///< `_id` 는 서식지 id, `_value` 는 서식지 번호이다
            HabitatLost,
            CreatureArrived,       ///< `_id` 는 종, `_value` 는 서식지 번호이다
            FriendshipTierChanged, ///< `_id` 는 종, `_detail` 은 새 단계, `_value` 는 호감도이다
            RequestCompleted,      ///< `_id` 는 종, `_detail` 은 퀘스트이다
            AbilityUsed,           ///< `_id` 는 종, `_detail` 은 얻은 아이템(없으면 비어 있다), `_value` 는 셀이다
            AppealTierChanged      ///< `_detail` 은 새 단계, `_value` 는 단계 번호이다
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
     * @brief 포코피아와 문스톤 아일랜드 류의 생활 규칙을 구현합니다.
     * @details 서식지는 셀이 바뀔 때마다 처음부터 다시 맞춘다. 차지하는 셀이 많은 레시피부터, 아래 줄에서 위 줄, 왼쪽에서 오른쪽, 회전 0에서 3 순서로 맞추며
     *          이미 다른 서식지가 차지한 셀은 쓰지 않는다(결정적). 전과 같은 레시피, 위치, 회전이면 같은 서식지로 보고(번호와 사는 생물을 유지한다),
     *          없어진 서식지의 생물은 좋아하는 다른 서식지에 자리가 있으면 옮겨 간다.
     *          방문은 `attractVisitors` 를 시마다 호출하면 처리한다. 자리가 남은 서식지마다, 그 서식지를 좋아하고 지금 시간대와 날씨에 오는 종(아직 마을에 없는 종)이
     *          `chance` 확률로 찾아온다. 확률은 (날, 시, 종, 서식지 위치, 시드)의 해시이므로 같은 시를 두 번 호출해도 같은 답이 나온다(두 번째는 아무 일도 일어나지 않는다).
     *          호감도는 빌린 기반 `ReputationState` 의 세력 하나(`creature.<종 id>`)가 생물 하나에 해당한다(감쇠는 공유 상태의 주인이 날이 넘어갈 때 처리한다). 대화와 선물은 각각 하루 한 번이고, 부탁(기반 `QuestLog`)을 끝내면 오른다.
     *          부탁 일지와 시계는 빌려 쓴다. 마을은 일지 이벤트를 꺼내지 않고 받은 부탁의 상태만 본다(이벤트는 게임 화면과 다른 키트가 쓴다). 날은 빌린 시계의 날이다.
     *          부탁 목표는 `Deliver`(아이템)와 `Habitat`(그 서식지 수)이고, 퀘스트 레벨 조건은 호감도 단계 번호로 판단한다.
     *          카탈로그는 빌려 쓰므로 마을보다 오래 살아야 한다.
     */
    class SW_KIT_API CreatureTown
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CTWN" );
        static constexpr uint32 kStateVersion = 1;

        CreatureTown();

        /**
         * @brief 크기를 정하고 모든 셀을 비웁니다. @p width 나 @p height 가 1 미만이면 오류를 남기고 false 입니다(조용히 1 로 맞추지 않습니다). 평판, 부탁 일지, 시계는 없어도 됩니다(평판이 없으면 호감도 0, 일지가 없으면 부탁 없음, 시계가 없으면 0일).
         * @param refs 빌려 쓰는 공유 상태이다. 마을은 `_pReputation`(호감도이며 세력 `creature.<종 id>`), `_pQuestLog`(부탁 일지), `_pClock`(날)을 쓰며 마을보다 오래 살아야 한다. 여러 장르가 섞인 게임은 `SessionStateComponent::makeRefs()` 로 얻는다.
         * @return 크기가 1 이상이면 true 입니다. 밖이면 오류를 남기고 false 이며 상태를 바꾸지 않습니다(값을 조용히 맞추지 않는다).
         */
        [[nodiscard]] bool initialize( const CreatureLifeCatalog* pCatalog, const SessionStateRefs& refs, int32 width, int32 height, const CreatureTownSettings& settings );

        /**
         * @brief 공유 땅을 빌립니다. 마을 셀 (0, 0) 이 땅 셀 @p origin 에 해당합니다. 이미 놓인 오브젝트의 셀을 얻고, 그 뒤로는 오브젝트를 놓을 때 얻고 비우면 반납합니다.
         * @return 이미 놓인 셀 하나라도 남의 땅이면 false 이며 땅을 묶지 않는다. @p pLand 가 nullptr 이면 빌린 땅을 반납하고 true 이다.
         */
        [[nodiscard]] bool bindLand( LandRegistry* pLand, const int2& origin );
        /** @brief 셀에 오브젝트를 놓고(빈 id 는 비우기) 서식지를 다시 맞춥니다. 밖이거나 그 셀의 공유 땅이 남의 것이면 false 를 반환하고 상태는 그대로입니다. */
        bool setObject( int32 x, int32 y, const hashed_string& object );
        /** @brief 시계와 날씨로 이번 시의 방문을 굴리고, 찾아온 수를 반환합니다. */
        int32 attractVisitors( const WorldClock& clock, const WeatherSystem& weather );
        /** @brief 날, 시, 시간대, 날씨를 직접 넘깁니다(테스트와 서버용). */
        int32 attractVisitorsAt( int32 day, int32 hour, DayPhase phase, const hashed_string& weatherID );

        CreatureInteractResult talkTo( const hashed_string& speciesID );
        /** @brief @p inventory 에서 하나를 빼서 줍니다. 좋아하는 선물, 음식, 그 밖의 아이템에 따라 점수가 다릅니다. */
        CreatureInteractResult giveGift( const hashed_string& speciesID, const hashed_string& itemID, Inventory& inventory );
        CreatureRequestResult  startRequest( const hashed_string& speciesID, const hashed_string& questID );
        /**
         * @brief 부탁의 `Deliver` 목표에 아이템을 건넵니다. 목표가 그것을 세었을 때만 @p inventory 에서 뺍니다.
         * @return 진행한 목표 수이다(0 이면 아무것도 빼지 않았다).
         */
        int32 deliverItem( const hashed_string& itemID, int32 count, Inventory& inventory );
        /** @brief 능력으로 셀을 바꿉니다. 얻은 아이템은 @p pYieldInventory(없어도 됩니다)에 넣습니다. */
        CreatureAbilityResult useAbility( const hashed_string& speciesID, const hashed_string& abilityID, int32 x, int32 y, Inventory* pYieldInventory );

        /** @brief 빈 셀에 집을 짓고 집 index 를 반환합니다. 밖이거나 셀이 차 있거나 공유 땅이 남의 것이면 -1 입니다. */
        int32               placeHouse( int32 x, int32 y, int32 capacity );
        CreatureHouseResult assignHouse( const hashed_string& speciesID, int32 houseIndex );
        /** @brief 집 없는 생물을 온 순서대로, 자리가 남은 가장 가까운 집(서식지 기준 맨해튼 거리이며 같으면 앞 집)에 넣고, 넣은 수를 반환합니다. */
        int32 assignHomeless();

        /** @brief 하루를 넘깁니다. 능력 횟수를 되돌리고 호감도가 감쇠합니다(대화와 선물은 시계의 날로 하루 한 번입니다). 디렉터가 시계의 날 넘김에 맞춰 호출합니다. */
        void advanceDay();
        void drainEvents( vector<CreatureTownEvent>& outListEvent );
        /** @brief 셀, 서식지(번호 포함), 생물, 집, 받은 부탁을 씁니다(핫 리로드와 세이브용). 평판, 부탁 일지, 시계는 빌린 것이라 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 마을 크기가 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다(카탈로그, 일지, 시계는 `initialize` 에서 받은 것을 씁니다). */
        [[nodiscard]] bool readState( Archive& archive );

        const hashed_string*           findObject( int32 x, int32 y ) const;
        const TownCreature*            findCreature( const hashed_string& speciesID ) const;
        const HabitatInstance*         findHabitat( int32 habitatInstanceID ) const;
        const vector<TownCreature>&    getCreatures() const { return _listCreature; }
        const vector<HabitatInstance>& getHabitats() const { return _listHabitat; }
        const vector<CreatureHouse>&   getHouses() const { return _listHouse; }
        /** @brief 그 레시피의 서식지 수를 반환합니다. */
        int32 countHabitats( const hashed_string& habitatID ) const;
        /** @brief 서식지에 머무는 생물 수를 반환합니다. */
        int32 countResidents( int32 habitatInstanceID ) const;
        /** @brief 오늘 남은 능력 횟수를 반환합니다. 모르면 0 입니다. */
        int32         countAbilityUsesLeft( const hashed_string& speciesID, const hashed_string& abilityID ) const;
        int32         getFriendship( const hashed_string& speciesID ) const;
        hashed_string getFriendshipTier( const hashed_string& speciesID ) const;
        /** @brief 사는 생물의 호감도 평균을 반환합니다(없으면 0). */
        float32 computeAverageFriendship() const;
        /** @brief 매력도 점수를 반환합니다(서식지 종류 수, 생물 수, 호감도 평균을 반영합니다). */
        float32 computeAppealScore() const;
        /** @brief 매력도 단계 번호를 반환합니다(단계가 없으면 -1). */
        int32         getAppealTierIndex() const { return _appealTier; }
        hashed_string getAppealTierName() const;
        /** @brief 빌린 시계의 날을 반환합니다(없으면 0). */
        int32 getDay() const;
        int32 getWidth() const { return _topology._width; }
        int32 getHeight() const { return _topology._height; }

    private:
        /** @brief 처음부터 다시 맞추고, 전 결과와 비교해 생기거나 없어진 서식지를 알립니다. */
        void refreshHabitats();
        /** @brief 패턴을 그 위치와 회전에 맞춰 봅니다. 맞으면 차지할 타일을 @p outListTile 에 적습니다. */
        bool  matchesAt( const HabitatDef& habitat, int32 rotation, int32 originX, int32 originY, const vector<uint8>& listClaimed,
                         vector<int32>& outListTile ) const;
        int32 findCreatureIndex( const hashed_string& speciesID ) const;
        bool  hasRoom( const HabitatInstance& instance ) const;
        void  notifyHabitatObjectives();
        /** @brief 호감도를 바꾸고 단계가 바뀌었으면 알립니다(빌린 평판의 이벤트는 꺼내지 않고, 앞뒤 단계를 비교합니다). */
        void changeFriendship( const hashed_string& speciesID, int32 delta );
        /** @brief 받은 부탁 중 끝난 것에 보상하고 목록에서 뺍니다(실패와 포기는 보상 없이 뺍니다). 일지 이벤트는 꺼내지 않습니다. */
        void collectCompletedRequests();
        /** @brief 부탁 @p questID 를 하는(마을에 사는) 생물에게 호감도를 줍니다. 준 생물이 있으면 true 를 반환합니다. */
        bool rewardRequest( const hashed_string& questID );
        void updateAppealTier();
        int2 computeCreatureAnchor( const TownCreature& creature ) const;
        bool isBlockingObject( const hashed_string& object ) const;

        vector<hashed_string>          _listObject; ///< 셀마다의 오브젝트 id 이다(`_topology` 의 타일 번호). 빈 id 는 빈 셀이다
        vector<HabitatInstance>        _listHabitat;
        vector<TownCreature>           _listCreature; ///< 온 순서
        vector<CreatureHouse>          _listHouse;
        vector<hashed_string>          _listOpenRequest; ///< 이 마을이 받은 부탁 중 아직 끝나지 않은 것이다(퀘스트 id)
        EventBuffer<CreatureTownEvent> _eventBuffer;
        CreatureTownSettings           _settings;
        const CreatureLifeCatalog*     _pCatalog;
        ReputationState*               _pReputation; ///< 빌린 평판이다(호감도이며 없으면 0)
        QuestLog*                      _pQuestLog;   ///< 빌린 부탁 일지(없으면 부탁 없음)
        const WorldClock*              _pClock;      ///< 빌린 시계이다(날이며 없으면 0일)
        GridTopology                   _topology;
        LandBinding                    _land; ///< 빌린 공유 땅(없으면 단독 동작)
        int32                          _nextHabitatID;
        int32                          _lastAttractKey; ///< 마지막으로 방문을 굴린 날 x 24 + 시
        int32                          _appealTier;
    };
} // namespace sw
