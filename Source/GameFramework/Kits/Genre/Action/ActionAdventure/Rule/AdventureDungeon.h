/**
 * @file AdventureDungeon.h
 * @brief 고전 젤다의 던전 규칙입니다. 던전마다 작은 열쇠, 보스 열쇠, 지도, 나침반이 있고, 열쇠로 여는 문과 아이템(갈고리, 폭탄, 활)이 조건인 문, 보물 상자, 장치(스위치, 눌림판, 시간제 스위치, 횃불 그룹)를 다룹니다.
 * @details 방 그래프는 기반 `AreaGraph` 가 가진다. 이 파일은 문이나 장치가 풀릴 때 `GameFlags` 플래그를 세우고, 그래프 연결의 `requires` 에 그 플래그 이름을 적으면 길이 열린다(`<Link from="hall" to="cell" requires="forest.door1"/>`).
 *          그래서 문 하나의 정의가 두 곳에 나뉘어 있다. 그래프는 "어디로 이어지는가"를 적고, 이 카탈로그는 "무엇으로 여는가"를 적는다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class AreaGraph;
    class GameFlags;
    class ItemStackList;
    class XMLNode;

    /** @brief 문을 무엇으로 여는지 나타냅니다. */
    enum class AdventureDoorKind : uint8
    {
        SmallKey = 0, ///< 그 던전의 작은 열쇠 하나를 쓴다
        BossKey,      ///< 그 던전의 보스 열쇠가 있어야 한다(열쇠를 소모하지 않는다. 보스 문은 하나뿐이다)
        Condition     ///< `GameFlags` 조건식(갈고리, 폭탄, 활 등. 예: `hasHookshot`, `bombs>=1`)이 참일 때 연다
    };

    /** @brief 문 하나입니다. 열리면 `_flag` 가 1이 됩니다. */
    struct AdventureDoorDef
    {
        hashed_string     _id{};
        hashed_string     _flag{};     ///< 비어 있으면 `door.<id>` 를 쓴다
        string            _requires{}; ///< `Condition` 문에 쓰는 조건식
        AdventureDoorKind _kind{ AdventureDoorKind::SmallKey };
    };
} // namespace sw

namespace sw
{
    /** @brief 보물 상자 하나입니다. 아이템 `SmallKey`, `BossKey`, `Map`, `Compass` 는 던전이 가져가고, 나머지는 게임에 넘깁니다. */
    struct AdventureTreasureDef
    {
        hashed_string _id{};
        hashed_string _area{}; ///< 상자가 놓인 방(나침반이 지도에 표시한다)
        hashed_string _item{};
        hashed_string _flag{}; ///< 열린 상자 플래그(비어 있으면 `treasure.<id>`)
        int32         _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 장치 종류입니다. */
    enum class AdventureDeviceKind : uint8
    {
        Switch = 0,    ///< 치면 켜진 채로 남는다(수정 스위치)
        PressurePlate, ///< 누르는 동안만 켜진다(`_bLatch` 가 true 면 한 번 눌러도 켜진 채로 남는다)
        TimedSwitch,   ///< 치면 `_duration` 초 동안 켜진다(시간제 문, 닫히기 전에 지나가야 한다)
        TorchGroup     ///< 횃불 `_torchCount` 개를 모두 켜면 풀린다. `_duration` 이 있으면 첫 불을 붙인 뒤 그 시간 안에 모두 켜야 하고, 못 켜면 모두 꺼진다
    };

    /** @brief 장치 하나입니다. 켜지면(풀리면) `_flag` 가 1, 꺼지면 0 입니다. */
    struct AdventureDeviceDef
    {
        hashed_string       _id{};
        hashed_string       _flag{}; ///< 비어 있으면 `device.<id>` 를 쓴다
        float32             _duration{ 0.0f };
        int32               _torchCount{ 1 };
        AdventureDeviceKind _kind{ AdventureDeviceKind::Switch };
        uint8               _bLatch{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 던전 하나입니다. */
    struct SW_KIT_API AdventureDungeonDef
    {
        hashed_string                _id{};
        hashed_string                _region{}; ///< `AreaGraph` 의 지역(지도가 한 번에 드러내는 단위)
        vector<AdventureDoorDef>     _listDoor{};
        vector<AdventureTreasureDef> _listTreasure{};
        vector<AdventureDeviceDef>   _listDevice{};

        const AdventureDoorDef*     findDoor( const hashed_string& id ) const;
        const AdventureTreasureDef* findTreasure( const hashed_string& id ) const;
        int32                       findDeviceIndex( const hashed_string& id ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdventureDungeonCatalog
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<AdventureDungeons><Dungeon id="forest" region="ForestTemple"><Door id="d1" kind="SmallKey" flag="forest.d1"/>
     *        <Door id="crack" kind="Condition" requires="bombs>=1"/><Treasure id="c1" area="hall" item="SmallKey"/>
     *        <Device id="eye" kind="Switch"/><Device id="torches" kind="TorchGroup" torches="2" duration="6"/></Dungeon></AdventureDungeons>`
     */
    class SW_KIT_API AdventureDungeonCatalog : public XMLCatalog<AdventureDungeonCatalog>, public GameCatalog<AdventureDungeonDef>
    {
        friend class XMLCatalog<AdventureDungeonCatalog>;

    public:
    private:
        static constexpr const utf8* kXMLRootName = "AdventureDungeons"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /** @brief 던전 하나의 진행 상태입니다(세이브 대상). 열쇠는 던전마다 따로 세므로 다른 던전의 열쇠로는 열 수 없습니다. */
    struct AdventureDungeonProgress
    {
        int32 _smallKeyCount{ 0 };
        int32 _smallKeyUsedCount{ 0 };
        uint8 _bBossKey{ SW_FALSE };
        uint8 _bMap{ SW_FALSE };
        uint8 _bCompass{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 문 열기 결과입니다. */
    ENUM()
    enum class AdventureDoorResult : uint8
    {
        Opened = 0,
        AlreadyOpen,
        UnknownDoor,
        NeedSmallKey,
        NeedBossKey,
        ConditionNotMet
    };

    /** @brief 던전 알림 종류입니다. */
    enum class AdventureDungeonEventType : uint8
    {
        DoorOpened = 0,
        TreasureOpened, ///< `_item` 과 `_count` 가 나온 것
        DeviceActivated,
        DeviceDeactivated, ///< 눌림판에서 내려왔거나 시간이 다 됐다
        TorchesFailed      ///< 시간 안에 횃불을 다 켜지 못해 모두 꺼졌다
    };

    /** @brief 던전 알림 하나입니다. */
    struct AdventureDungeonEvent
    {
        hashed_string             _dungeon{};
        hashed_string             _id{}; ///< 문, 상자, 장치의 id
        hashed_string             _item{};
        int32                     _count{ 0 };
        AdventureDungeonEventType _type{ AdventureDungeonEventType::DoorOpened };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdventureDungeonState
     * @brief 모든 던전의 진행 상태와 장치 상태입니다. 열린 문, 상자, 풀린 장치는 `GameFlags` 에 있으므로 플래그 세이브가 곧 던전 세이브입니다.
     *        열쇠 개수 같은 `AdventureDungeonProgress` 만 따로 저장합니다.
     * @details 시간은 `update` 로만 흐른다(결정적). 카탈로그는 빌려 쓴다.
     */
    class SW_KIT_API AdventureDungeonState
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "ADGN" );
        static constexpr uint32 kStateVersion = 1;

        AdventureDungeonState();

        void initialize( const AdventureDungeonCatalog* pCatalog );

        /** @brief 작은 열쇠를 줍습니다. 상자 밖에서 줍는 경우로, 적이 떨어뜨린 열쇠가 해당합니다. 모르는 던전이면 false 를 반환합니다. */
        bool addSmallKey( const hashed_string& dungeonID, int32 count = 1 );
        /** @brief 문을 엽니다. 작은 열쇠 문은 열쇠 하나를 씁니다. */
        AdventureDoorResult openDoor( const hashed_string& dungeonID, const hashed_string& doorID, GameFlags& flags );
        /**
         * @brief 상자를 엽니다. 던전 아이템(작은 열쇠, 보스 열쇠, 지도, 나침반)은 던전이 가져가고, 나머지는 @p outReward 에 더합니다.
         * @return 처음 열었으면 true 입니다(모르는 상자나 이미 연 상자는 false).
         */
        [[nodiscard]] bool openTreasure( const hashed_string& dungeonID, const hashed_string& treasureID, GameFlags& flags, ItemStackList& outReward );
        /** @brief 지도가 있으면 던전 지역의 방을 모두 드러내고, 새로 드러난 방 수를 반환합니다. 지도가 없으면 0 입니다. */
        int32 revealMap( const hashed_string& dungeonID, AreaGraph& areaGraph ) const;
        /** @brief 나침반이 있으면 아직 열지 않은 상자를 @p outListTreasure 에 담습니다. 나침반이 없으면 목록을 비웁니다. */
        void collectCompassMarker( const hashed_string& dungeonID, const GameFlags& flags, vector<const AdventureTreasureDef*>& outListTreasure ) const;

        /** @brief 스위치나 시간제 스위치를 칩니다. 시간제 스위치는 다시 치면 시간이 처음부터 시작됩니다. 켜졌으면 true 를 반환합니다. */
        bool hitSwitch( const hashed_string& dungeonID, const hashed_string& deviceID, GameFlags& flags );
        /** @brief 눌림판 위에 무엇이 올라섰는지(사람, 상자, 쇠공) 알립니다. */
        void setPlatePressed( const hashed_string& dungeonID, const hashed_string& deviceID, bool bPressed, GameFlags& flags );
        /** @brief 횃불 하나에 불을 붙입니다(불화살, 디쿠의 막대). 이번에 새로 켜진 횃불이 있으면 true 를 반환합니다. */
        bool lightTorch( const hashed_string& dungeonID, const hashed_string& deviceID, GameFlags& flags );
        /** @brief 시간을 흘립니다. 시간제 스위치가 꺼지고, 시간 안에 다 켜지 못한 횃불이 꺼집니다. */
        void update( float32 deltaTime, GameFlags& flags );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<AdventureDungeonEvent>& outListEvent );

        const AdventureDungeonProgress* findProgress( const hashed_string& dungeonID ) const;
        bool                            isDeviceActive( const hashed_string& dungeonID, const hashed_string& deviceID ) const;
        int32                           getLitTorchCount( const hashed_string& dungeonID, const hashed_string& deviceID ) const;

        /**
         * @brief 던전마다 id, 진행(열쇠, 지도, 나침반), 장치(남은 시간, 켜진 횃불 수, 활성)를 씁니다.
         *        열린 문, 상자, 장치 플래그는 빌려 온 `GameFlags` 의 것이라 싣지 않습니다. 카탈로그는 `initialize` 에서 받은 것을 쓰고, 알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 카탈로그에 없는 던전이거나 장치 수가 다르거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 장치 하나의 지금 상태입니다. */
        struct DeviceRuntime
        {
            Countdown _timer{}; ///< 시간제 스위치와 횃불 그룹의 남은 시간
            int32     _litCount{ 0 };
            uint8     _bActive{ SW_FALSE };
        };

        /** @brief 던전 하나의 지금 상태입니다. */
        struct DungeonRuntime
        {
            AdventureDungeonProgress _progress{};
            vector<DeviceRuntime>    _listDevice{};
        };

        DungeonRuntime* findRuntime( const hashed_string& dungeonID, const AdventureDungeonDef** ppOutDef );
        void            setDeviceActive( const AdventureDungeonDef& dungeon, int32 deviceIndex, bool bActive, GameFlags& flags );
        void            pushEvent( AdventureDungeonEventType type, const hashed_string& dungeonID, const hashed_string& id, const hashed_string& item = hashed_string{},
                                   int32 count = 0 );

        const AdventureDungeonCatalog*     _pCatalog;
        vector<DungeonRuntime>             _listRuntime; ///< 카탈로그의 던전 순서
        EventBuffer<AdventureDungeonEvent> _eventBuffer;
    };
} // namespace sw
