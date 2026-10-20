/**
 * @file ScheduleCatalog.h
 * @brief NPC 하루 일정의 데이터를 정의합니다.
 * @details 달력 어휘, 장소, 활동 자리, 끼어들기 종류, 약속, 묶음(아키타입), NPC 루틴, 축제와 행사를 담는다.
 *          사람이 고치는 데이터(`*.schedules.xml`)이다.
 *          루틴은 조건, 우선순위, 시간 칸으로 이루어진다. 시간마다 조건이 맞는 칸 중 우선순위가 가장 높은 것이 이긴다.
 *          축제는 우선순위가 높은 행사 칸이라서, 그 시간만 덮고 나머지는 평소 루틴이 그대로 남는다.
 *          스타듀 밸리의 일정과 축제, 스카이림의 Radiant AI 패키지, 동물의 숲 주민의 하루, 페르소나의 요일 일정, RDR2 의 마을 사람이 같은 모양이다.
 *          활동은 `ScheduleActivityRegistry` 의 이름으로 고른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ScheduleActivityRegistry;
    class XMLNode;

    /** @brief 하루의 분 수입니다. 일정 시각은 0..1440 분(정수)입니다. */
    inline constexpr int32 kScheduleMinutesPerDay = 1440;

    /** @brief 일정이 가는 장소 하나입니다. */
    struct SchedulePlaceDef
    {
        hashed_string _id{};
        hashed_string _area{}; ///< `AreaGraph` 방 id(지역 단위 시뮬레이션)
        float3        _position{};
        float32       _radius{ 0.0f }; ///< 돌아다니기 범위의 기본값
    };
} // namespace sw

namespace sw
{
    /** @brief 활동 자리 하나입니다. 벤치나 작업대 같은 자리이며, 스마트 오브젝트가 없을 때 `ScheduleSpotLocator` 가 나눠 줍니다. */
    struct ScheduleSpotDef
    {
        hashed_string _id{};
        hashed_string _kind{};
        hashed_string _area{};
        float3        _position{};
        int32         _capacity{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 끼어들기 종류(말 걸기, 전투, 경보)입니다. 우선순위가 높은 것이 위에 쌓입니다. */
    struct ScheduleInterruptDef
    {
        hashed_string _id{};
        int32         _priority{ 0 };
        int32         _timeoutMinutes{ 0 }; ///< 이만큼 지나면 저절로 끝난다(0 = 게임이 끝낼 때까지)
    };
} // namespace sw

namespace sw
{
    /** @brief 일정 칸 하나입니다. 그 시간에 할 활동을 담습니다. */
    struct ScheduleBlockDef
    {
        ScheduleCondition _condition{};
        hashed_string     _activity{};
        hashed_string     _place{};
        hashed_string     _objectKind{};  ///< `UseObject` — 스마트 오브젝트 종류
        hashed_string     _appointment{}; ///< `Meet` — 시간 · 장소는 약속의 것
        hashed_string     _animation{};   ///< 비면 활동 종류의 기본 애니메이션
        int32             _startMinute{ 0 };
        int32             _endMinute{ 0 };
        int32             _wanderMinutes{ 20 }; ///< `Wander` — 한 자리에 머무는 분
        float32           _radius{ -1.0f };     ///< `Wander` — 음수면 장소의 반지름
    };
} // namespace sw

namespace sw
{
    /** @brief 루틴입니다. 조건이 맞는 날 쓰는 일정 칸의 묶음입니다. */
    struct ScheduleRoutineDef
    {
        hashed_string            _id{};
        ScheduleCondition        _condition{};
        vector<ScheduleBlockDef> _listBlock{};
        int32                    _priority{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 묶음(아키타입)입니다. 여러 NPC 가 나누는 루틴이며, 부모 묶음의 루틴을 이어받습니다. */
    struct ScheduleArchetypeDef
    {
        hashed_string              _id{};
        hashed_string              _parent{};
        vector<ScheduleRoutineDef> _listRoutine{};
    };
} // namespace sw

namespace sw
{
    /** @brief NPC 하나입니다. 루틴은 자기 것과 묶음 사슬의 것을 읽을 때 모아 둡니다. 같은 id 면 자기 것이 덮습니다. */
    struct ScheduleNpcDef
    {
        static constexpr float32 kMinUnitsPerMinute = 0.01f; ///< `speed` 의 하한(0 이면 이동이 끝나지 않는다)

        hashed_string              _id{};
        hashed_string              _archetype{};
        hashed_string              _home{};
        hashed_string              _idleActivity{}; ///< 어느 칸도 없는 시간의 활동(기본 `StayHome`)
        vector<TagID>              _listTag{};      ///< 처음 태그(직업 · 성격)
        vector<ScheduleRoutineDef> _listRoutine{};  ///< 자기 것 다음 묶음 사슬의 것
        float32                    _unitsPerMinute{ 60.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 축제나 행사입니다. 조건이 맞는 날 참가자의 그 시간을 덮는 칸입니다. */
    struct ScheduleEventDef
    {
        hashed_string            _id{};
        ScheduleCondition        _condition{};
        vector<hashed_string>    _listNpc{};       ///< 참가 NPC id
        vector<hashed_string>    _listArchetype{}; ///< 참가 묶음(사슬 어디에 있어도) — 둘 다 비면 모두
        vector<ScheduleBlockDef> _listBlock{};
        int32                    _priority{ 100 };
    };
} // namespace sw

namespace sw
{
    /** @brief 약속입니다. 두 NPC 이상의 일정이 함께 가리키는 만남(시간과 장소)이며, 참가자는 `Meet` 칸이 이 약속을 가리키는 NPC 입니다. */
    struct ScheduleAppointmentDef
    {
        hashed_string         _id{};
        hashed_string         _place{};
        ScheduleCondition     _condition{};
        vector<hashed_string> _listAttendee{}; ///< 읽을 때 채운다(읽은 순서)
        int32                 _startMinute{ 0 };
        int32                 _endMinute{ 0 };
        int32                 _waitMinutes{ 30 }; ///< 시작 뒤 이만큼 기다려도 모두 오지 않으면 그날 약속은 깨진다
        int32                 _priority{ 50 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScheduleCatalog
     * @brief `<Schedules>` XML 을 읽습니다.
     * @code
     *     <Schedules>
     *       <Calendar days="Mon,Tue,Wed,Thu,Fri,Sat,Sun" seasons="Spring,Summer,Fall,Winter" weathers="sunny,rain"/>
     *       <Place id="store" area="town" position="20 0 4" radius="2"/>
     *       <Spot id="bench_a" kind="Bench" area="park" position="30 0 10" capacity="1"/>
     *       <Interrupt id="Talk" priority="10" timeout="30"/>
     *       <Appointment id="lunch" place="saloon" start="12:00" end="13:00" wait="30" priority="50" days="Fri"/>
     *       <Archetype id="villager"><Routine id="base"><Block start="0:00" end="7:00" activity="Sleep"/></Routine></Archetype>
     *       <Npc id="pierre" archetype="villager" home="pierre_home" speed="60" tags="Job.Shopkeeper">
     *         <Routine id="weekday" priority="0" days="Mon,Tue,Wed,Thu,Fri">
     *           <Block start="9:00" end="17:00" activity="WorkAt" place="store" animation="Sweep"/>
     *           <Block activity="Meet" appointment="lunch"/>
     *         </Routine>
     *         <Routine id="rainy" priority="10" weathers="rain"><Block start="17:00" end="22:00" activity="StayHome"/></Routine>
     *       </Npc>
     *       <Event id="egg_festival" priority="100" seasons="Spring" daysOfSeason="13" archetypes="villager">
     *         <Block start="9:00" end="14:00" activity="Attend" place="square"/>
     *       </Event>
     *     </Schedules>
     * @endcode
     * @details 시각은 `H:MM`(0:00..24:00)으로 적는다. 자정을 넘는 칸은 `overnight="true"` 를 적고(`22:00`–`6:00`) 두 칸으로 나눠 읽는다.
     *          모르는 이름(원소, 속성, 활동, 장소, 약속, 묶음, 요일, 계절, 날씨), 겹치는 칸, 시작 ≥ 끝, 묶음 순환은 경고하고 그 항목을 뺀다.
     *          `ResourceDataSchemaTest` 가 그 경고를 실패로 잡는다.
     */
    class SW_GF_API ScheduleCatalog : public XMLCatalog<ScheduleCatalog>
    {
        friend class XMLCatalog<ScheduleCatalog>;

    public:
        ScheduleCatalog();

        void clear();

        /** @brief "9:30", "24:00", "9" 같은 글을 분으로 읽습니다. 범위(0..1440) 밖이거나 틀린 글이면 false 입니다. */
        [[nodiscard]] static bool parseClockMinutes( string_view text, int32& outMinutes );

        const ScheduleActivityRegistry& getActivityRegistry() const;
        /** @brief 활동 이름을 검사할 레지스트리를 정합니다. 주지 않으면 기본 종류만 든 레지스트리를 쓰며, 읽기 전에 정해야 합니다. */
        void                                  setActivityRegistry( const ScheduleActivityRegistry* pRegistry ) { _pActivityRegistry = pRegistry; }
        const ScheduleConditionVocabulary&    getVocabulary() const { return _vocabulary; }
        const SchedulePlaceDef*               findPlace( const hashed_string& id ) const { return _placeCatalog.find( id ); }
        const ScheduleNpcDef*                 findNpc( const hashed_string& id ) const { return _npcCatalog.find( id ); }
        const ScheduleArchetypeDef*           findArchetype( const hashed_string& id ) const { return _archetypeCatalog.find( id ); }
        const ScheduleAppointmentDef*         findAppointment( const hashed_string& id ) const { return _appointmentCatalog.find( id ); }
        int32                                 findAppointmentIndex( const hashed_string& id ) const { return _appointmentCatalog.findIndex( id ); }
        const ScheduleInterruptDef*           findInterrupt( const hashed_string& id ) const { return _interruptCatalog.find( id ); }
        const vector<SchedulePlaceDef>&       getPlaces() const { return _placeCatalog.getAll(); }
        const vector<ScheduleSpotDef>&        getSpots() const { return _spotCatalog.getAll(); }
        const vector<ScheduleNpcDef>&         getNpcs() const { return _npcCatalog.getAll(); }
        const vector<ScheduleEventDef>&       getEvents() const { return _eventCatalog.getAll(); }
        const vector<ScheduleAppointmentDef>& getAppointments() const { return _appointmentCatalog.getAll(); }
        const vector<ScheduleInterruptDef>&   getInterrupts() const { return _interruptCatalog.getAll(); }
        /** @brief 묶음 사슬(자기부터 부모 쪽으로)에 @p archetypeID 가 있으면 true 입니다. */
        bool isNpcOfArchetype( const ScheduleNpcDef& npc, const hashed_string& archetypeID ) const;

    private:
        static constexpr const utf8* kXMLRootName = "Schedules"; ///< 루트 원소(`XMLCatalog`)

        uint32 loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 읽습니다. 읽은 NPC 수(0 또는 1)를 반환합니다. */
        uint32 loadElement( const XMLNode& node, string_view sourceName );
        /** @brief 루틴 하나를 읽습니다. id 가 없거나 우선순위 · 조건이 깨졌으면 오류를 남기고 false 입니다(부르는 쪽이 넣지 않는다). */
        [[nodiscard]] bool readRoutine( const XMLNode& node, string_view sourceName, string_view ownerName, ScheduleRoutineDef& outRoutine ) const;
        /**
         * @brief 칸을 읽어 @p inoutListBlock 에 더합니다. 쓸 수 없는 칸이면 경고하고 더하지 않습니다. @p bInRoutine 이면 `Meet` 를 받습니다.
         * @details 자정을 넘는 칸(`overnight="true" start="22:00" end="6:00"`)은 두 칸 `[22:00, 24:00)` 과 `[0:00, 6:00)` 으로 나눠 더합니다. 조건은 두 칸이 같고 각 칸이 그날에 대해 따로 판정합니다.
         *          `overnight` 표시가 없는 start ≥ end 는 실수라서 경고하고 뺍니다.
         */
        void readBlock( const XMLNode& node, string_view sourceName, string_view ownerName, bool bInRoutine, vector<ScheduleBlockDef>& inoutListBlock ) const;
        /** @brief 칸 하나를 읽습니다. 자정을 넘는 칸이면 @p outTailEndMinute 에 다음 날 새벽 칸의 끝(분)을 적고 `outBlock` 은 24:00 에서 끝납니다. 아니면 −1 입니다. */
        [[nodiscard]] bool readSingleBlock( const XMLNode& node, string_view sourceName, string_view ownerName, bool bInRoutine, ScheduleBlockDef& outBlock,
                                            int32& outTailEndMinute ) const;
        void               warnOverlappingBlocks( const vector<ScheduleBlockDef>& listBlock, string_view sourceName, string_view ownerName ) const;
        /** @brief NPC 마다 묶음 사슬의 루틴을 이어 붙이고 약속 참가자를 모읍니다. 순환이나 모르는 이름은 경고합니다. */
        void resolveReferences( string_view sourceName );

        GameCatalog<SchedulePlaceDef>       _placeCatalog;
        GameCatalog<ScheduleSpotDef>        _spotCatalog;
        GameCatalog<ScheduleInterruptDef>   _interruptCatalog;
        GameCatalog<ScheduleAppointmentDef> _appointmentCatalog;
        GameCatalog<ScheduleArchetypeDef>   _archetypeCatalog;
        GameCatalog<ScheduleNpcDef>         _npcCatalog;
        GameCatalog<ScheduleEventDef>       _eventCatalog;
        ScheduleConditionVocabulary         _vocabulary;
        const ScheduleActivityRegistry*     _pActivityRegistry;
    };
} // namespace sw
