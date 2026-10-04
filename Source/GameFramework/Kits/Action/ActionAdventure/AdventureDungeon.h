/**
 * @file AdventureDungeon.h
 * @brief 고전 젤다 던전 규칙 — 던전마다의 작은 열쇠 · 보스 열쇠 · 지도 · 나침반, 열쇠 문 · 아이템 조건 문(갈고리 · 폭탄 · 활), 보물 상자,
 *        장치(스위치 · 눌림판 · 시간제 스위치 · 횃불 묶음)입니다.
 * @details 방 그래프는 기반 `AreaGraph` 가 듭니다. 이 파일은 문 · 장치가 풀릴 때 `GameFlags` 플래그를 세우고, 그래프 연결의 `requires` 가
 *          그 플래그 이름을 적어 길이 열립니다(`<Link from="hall" to="cell" requires="forest.door1"/>`). 그래서 문 하나의 정의가 두 곳에
 *          나뉘지만, 그래프는 "어디로 이어지는가" 를, 이 카탈로그는 "무엇으로 여는가" 를 적습니다.
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
#include "GameFramework/Utility/Countdown.h"
#include "GameFramework/Utility/EventBuffer.h"

namespace sw
{
    class AreaGraph;
    class GameFlags;
    class ItemBag;
    class XmlNode;

    /** @brief 문이 무엇으로 열리는가입니다. */
    enum class AdventureDoorKind : uint8
    {
        SmallKey = 0, ///< 그 던전의 작은 열쇠 하나를 쓴다
        BossKey,      ///< 그 던전의 보스 열쇠가 있어야 한다(쓰지 않는다 — 보스 문은 하나)
        Condition     ///< `GameFlags` 조건식(갈고리 · 폭탄 · 활 — `hasHookshot`, `bombs>=1`)이 참일 때 연다
    };

    /** @brief 문 하나입니다. 열리면 `_flag` 가 1 이 됩니다. */
    struct AdventureDoorDef
    {
        hashed_string     _id{};
        hashed_string     _flag{};     ///< 비면 id
        string            _requires{}; ///< `Condition` 문의 조건식
        AdventureDoorKind _kind{ AdventureDoorKind::SmallKey };
    };
} // namespace sw

namespace sw
{
    /** @brief 보물 상자 하나입니다. 아이템 `SmallKey` · `BossKey` · `Map` · `Compass` 는 던전이 거두고 나머지는 게임에 넘깁니다. */
    struct AdventureTreasureDef
    {
        hashed_string _id{};
        hashed_string _area{}; ///< 놓인 방(나침반이 지도에 찍는다)
        hashed_string _item{};
        hashed_string _flag{}; ///< 열린 상자 — 비면 id
        int32         _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 장치 종류입니다. */
    enum class AdventureDeviceKind : uint8
    {
        Switch = 0,    ///< 치면 켜진 채로 남는다(수정 스위치)
        PressurePlate, ///< 누르는 동안만 켜진다(`_bLatch` 면 한 번 누르면 남는다)
        TimedSwitch,   ///< 치면 `_duration` 초 동안 켜진다(시간제 문 — 닫히기 전에 지나가라)
        TorchGroup     ///< 횃불 `_torchCount` 개를 모두 켜면 풀린다. `_duration` 이 있으면 첫 불부터 그 안에 다 켜야 한다(아니면 꺼진다)
    };

    /** @brief 장치 하나입니다. 켜지면(풀리면) `_flag` 가 1, 꺼지면 0 입니다. */
    struct AdventureDeviceDef
    {
        hashed_string       _id{};
        hashed_string       _flag{}; ///< 비면 id
        float32             _duration{ 0.0f };
        int32               _torchCount{ 1 };
        AdventureDeviceKind _kind{ AdventureDeviceKind::Switch };
        uint8               _bLatch{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 던전 하나입니다. */
    struct SW_GF_API AdventureDungeonDef
    {
        hashed_string                _id{};
        hashed_string                _region{}; ///< `AreaGraph` 의 지역 — 지도가 드러내는 단위
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
     * @brief `<AdventureDungeons><Dungeon id="forest" region="ForestTemple"><Door id="d1" kind="SmallKey" flag="forest.d1"/>
     *        <Door id="crack" kind="Condition" requires="bombs>=1"/><Treasure id="c1" area="hall" item="SmallKey"/>
     *        <Device id="eye" kind="Switch"/><Device id="torches" kind="TorchGroup" torches="2" duration="6"/></Dungeon></AdventureDungeons>` 를 읽습니다.
     */
    class SW_GF_API AdventureDungeonCatalog : public XmlCatalog<AdventureDungeonCatalog>
    {
        friend class XmlCatalog<AdventureDungeonCatalog>;

    public:
        const AdventureDungeonDef*         findDungeon( const hashed_string& id ) const { return _catalog.find( id ); }
        int32                              findDungeonIndex( const hashed_string& id ) const { return _catalog.findIndex( id ); }
        const vector<AdventureDungeonDef>& getDungeons() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXmlRootName = "AdventureDungeons"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<AdventureDungeonDef> _catalog{};
    };
} // namespace sw

namespace sw
{
    /** @brief 던전 하나의 진행(세이브 대상)입니다. 열쇠는 던전마다 따로 셉니다 — 다른 던전의 열쇠로는 열 수 없습니다. */
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
    enum class AdventureDoorResult : uint8
    {
        Opened = 0,
        AlreadyOpen,
        UnknownDoor,
        NeedSmallKey,
        NeedBossKey,
        ConditionNotMet
    };

    SW_GF_API const utf8* toString( AdventureDoorResult result );

    /** @brief 던전 알림 종류입니다. */
    enum class AdventureDungeonEventType : uint8
    {
        DoorOpened = 0,
        TreasureOpened, ///< `_item` · `_count` = 나온 것
        DeviceActivated,
        DeviceDeactivated, ///< 눌림판에서 내려왔다 · 시간이 다 됐다
        TorchesFailed      ///< 시간 안에 횃불을 다 켜지 못해 모두 꺼졌다
    };

    /** @brief 던전 알림 하나입니다. */
    struct AdventureDungeonEvent
    {
        hashed_string             _dungeon{};
        hashed_string             _id{}; ///< 문 · 상자 · 장치 id
        hashed_string             _item{};
        int32                     _count{ 0 };
        AdventureDungeonEventType _type{ AdventureDungeonEventType::DoorOpened };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdventureDungeonState
     * @brief 모든 던전의 진행과 장치 상태입니다. 열린 문 · 상자 · 풀린 장치는 `GameFlags` 에 있어 플래그 세이브가 곧 던전 세이브입니다
     *        (열쇠 개수 등 `AdventureDungeonProgress` 만 따로).
     * @details 시간은 `update` 로만 흐릅니다(결정적). 카탈로그는 빌려 씁니다.
     */
    class SW_GF_API AdventureDungeonState
    {
    public:
        AdventureDungeonState();

        void initialize( const AdventureDungeonCatalog* pCatalog );

        /** @brief 작은 열쇠를 줍습니다(상자 밖 — 적이 떨어뜨림). 모르는 던전이면 false 입니다. */
        bool addSmallKey( const hashed_string& dungeonId, int32 count = 1 );
        /** @brief 문을 엽니다. 작은 열쇠 문은 열쇠 하나를 씁니다. */
        AdventureDoorResult openDoor( const hashed_string& dungeonId, const hashed_string& doorId, GameFlags& flags );
        /**
         * @brief 상자를 엽니다. 던전 아이템(작은 열쇠 · 보스 열쇠 · 지도 · 나침반)은 여기서 거두고 나머지는 @p outReward 에 더합니다.
         * @return 처음 열었으면 true(모르는 상자 · 이미 연 상자는 false).
         */
        [[nodiscard]] bool openTreasure( const hashed_string& dungeonId, const hashed_string& treasureId, GameFlags& flags, ItemBag& outReward );
        /** @brief 지도가 있으면 던전 지역의 방을 모두 드러냅니다. 새로 드러난 수입니다(지도가 없으면 0). */
        int32 revealMap( const hashed_string& dungeonId, AreaGraph& areaGraph ) const;
        /** @brief 나침반이 있으면 아직 열지 않은 상자들입니다(없으면 비운다). */
        void collectCompassMarker( const hashed_string& dungeonId, const GameFlags& flags, vector<const AdventureTreasureDef*>& outListTreasure ) const;

        /** @brief 스위치 · 시간제 스위치를 칩니다(시간제는 다시 치면 시간이 처음부터). 켜졌으면 true 입니다. */
        bool hitSwitch( const hashed_string& dungeonId, const hashed_string& deviceId, GameFlags& flags );
        /** @brief 눌림판 위에 무엇이 올라섰는가(사람 · 상자 · 쇠공)를 알립니다. */
        void setPlatePressed( const hashed_string& dungeonId, const hashed_string& deviceId, bool bPressed, GameFlags& flags );
        /** @brief 횃불 하나에 불을 붙입니다(불화살 · 디쿠의 막대). 이번에 하나가 새로 켜졌으면 true 입니다. */
        bool lightTorch( const hashed_string& dungeonId, const hashed_string& deviceId, GameFlags& flags );
        /** @brief 시간을 흘립니다 — 시간제 스위치가 꺼지고, 시간 안에 다 켜지 못한 횃불이 꺼집니다. */
        void update( float32 deltaTime, GameFlags& flags );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<AdventureDungeonEvent>& outListEvent );

        const AdventureDungeonProgress* findProgress( const hashed_string& dungeonId ) const;
        bool                            isDeviceActive( const hashed_string& dungeonId, const hashed_string& deviceId ) const;
        int32                           getLitTorchCount( const hashed_string& dungeonId, const hashed_string& deviceId ) const;

    private:
        /** @brief 장치 하나의 지금 상태입니다. */
        struct DeviceRuntime
        {
            Countdown _timer{}; ///< 시간제 스위치 · 횃불 묶음의 남은 시간
            int32     _litCount{ 0 };
            uint8     _bActive{ SW_FALSE };
        };

        /** @brief 던전 하나의 지금 상태입니다. */
        struct DungeonRuntime
        {
            AdventureDungeonProgress _progress{};
            vector<DeviceRuntime>    _listDevice{};
        };

        DungeonRuntime* findRuntime( const hashed_string& dungeonId, const AdventureDungeonDef** ppOutDef );
        void            setDeviceActive( const AdventureDungeonDef& dungeon, int32 deviceIndex, bool bActive, GameFlags& flags );
        void            pushEvent( AdventureDungeonEventType type, const hashed_string& dungeonId, const hashed_string& id, const hashed_string& item = hashed_string{},
                                   int32 count = 0 );

        const AdventureDungeonCatalog*     _pCatalog;
        vector<DungeonRuntime>             _listRuntime; ///< 카탈로그 던전 순서
        EventBuffer<AdventureDungeonEvent> _eventBuffer;
    };
} // namespace sw
