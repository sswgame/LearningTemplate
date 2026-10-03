/**
 * @file GhostCatalog.h
 * @brief 유령 사냥(루이지 맨션 장르)의 데이터 — 손전등 · 청소기 수치, 유령 종류, 저택의 방(유령 · 열쇠 보상 · 불 플래그) · 열쇠 문 · 가구 · 부(Boo)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 손전등 수치입니다. 각은 원뿔의 절반(도)입니다. */
    struct GhostFlashlightSettings
    {
        float32 _range{ 8.0f };
        float32 _halfAngle{ 25.0f };
        float32 _strobeRange{ 10.0f };
        float32 _strobeHalfAngle{ 40.0f };
        float32 _strobeChargeTime{ 1.0f }; ///< 이만큼 모아야 스트로브가 터진다(덜 모으면 헛번쩍)
    };

    /** @brief 청소기 수치입니다. */
    struct GhostVacuumSettings
    {
        vector<float32> _listStagePower{};       ///< 강화 단계마다 초당 피해(0 단계부터)
        float32         _range{ 4.0f };          ///< 기절한 유령을 빨아들이기 시작할 수 있는 거리
        float32         _alignThreshold{ 0.7f }; ///< 당기는 방향 · 도망 반대 방향의 내적이 이 이상이면 "반대로 당긴다"
        float32         _alignBonus{ 1.5f };     ///< 반대로 당길 때 피해 배율
        float32         _surgeFillTime{ 2.0f };  ///< 반대로 이만큼 당기면 순간 흡입(서지) 게이지가 찬다
        float32         _surgeDamage{ 40.0f };
        float32         _dragSpeed{ 2.0f };      ///< 유령이 플레이어를 끄는 초당 거리(유령의 `_pull` 배)
        float32         _dragReduction{ 0.75f }; ///< 반대로 당기면 끌림이 이 비율만큼 준다
    };

    /** @brief 유령 한 종류입니다. 시간은 초입니다. */
    struct GhostDef
    {
        hashed_string _id{};
        float32       _hp{ 40.0f };
        float32       _hideTime{ 2.0f };   ///< 숨어 있다가 나타날 때까지
        float32       _appearTime{ 1.5f }; ///< 나타나 있다가 공격할 때까지
        float32       _attackTime{ 1.0f }; ///< 공격 동작(이 끝에 맞는다) — 이 동안 심장이 드러나 손전등에 기절한다
        float32       _attackDamage{ 8.0f };
        float32       _stunTime{ 3.0f };
        float32       _pull{ 1.0f };            ///< 끄는 힘 배율
        float32       _fleeInterval{ 1.0f };    ///< 흡입 중 도망 방향을 바꾸는 간격
        int32         _coins{ 10 };             ///< 잡으면 떨어뜨리는 동전
        uint8         _bStrobeOnly{ SW_FALSE }; ///< 보통 손전등으로는 기절하지 않는다
    };

    /** @brief 저택의 방 하나입니다(그래프는 `AreaGraph`). */
    struct GhostRoomDef
    {
        hashed_string         _id{};
        hashed_string         _keyReward{}; ///< 불이 켜지면 주는 열쇠
        hashed_string         _lightFlag{}; ///< 불이 켜지면 1 — 비면 "lit." + id
        vector<hashed_string> _listGhost{};
    };

    /** @brief 열쇠 문입니다. 열리면 `_flag` 가 1 이고 그래프 연결의 `requires` 가 그것을 봅니다. */
    struct GhostDoorDef
    {
        hashed_string _id{};
        hashed_string _key{};
        hashed_string _flag{};
    };

    /** @brief 가구를 뒤지는 방법입니다. */
    enum class GhostSearchMode : uint8
    {
        Vacuum = 0, ///< 빨아들이기(커튼 · 식탁보)
        Shake       ///< 흔들기 · 살피기(서랍 · 옷장)
    };

    /** @brief 가구 하나입니다. */
    struct GhostFurnitureDef
    {
        hashed_string _id{};
        hashed_string _room{};
        hashed_string _lootTable{};
        uint8         _bVacuum{ SW_TRUE };
        uint8         _bShake{ SW_TRUE };
    };

    /** @brief 부(Boo) 하나입니다 — 가구에 숨고, 들키면 `_escapeTime` 안에 못 잡으면 다른 방으로 달아납니다. */
    struct GhostBooDef
    {
        hashed_string _id{};
        hashed_string _room{};
        hashed_string _furniture{};
        float32       _hp{ 30.0f };
        float32       _escapeTime{ 5.0f };
    };

    /**
     * @class GhostCatalog
     * @brief `<GhostHunt><Flashlight range="8" angle="25" strobeRange="10" strobeAngle="40" strobeCharge="1"/>
     *        <Vacuum range="4" stages="10 16 24" alignThreshold="0.7" alignBonus="1.5" surgeFill="2" surgeDamage="40" dragSpeed="2" dragReduction="0.75"/>
     *        <Ghost id="goob" hp="40" .../><Room id="foyer" ghosts="goob,goob" key="parlorKey"/><Door id="parlorDoor" key="parlorKey" flag="door.parlor"/>
     *        <Furniture id="dresser" room="foyer" loot="dresserLoot" search="Shake"/><Boo id="booA" room="foyer" furniture="dresser" hp="30" escapeTime="5"/></GhostHunt>`
     *        를 읽습니다.
     */
    class SW_GF_API GhostCatalog
    {
    public:
        GhostCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        const GhostFlashlightSettings&   getFlashlight() const { return _flashlight; }
        const GhostVacuumSettings&       getVacuum() const { return _vacuum; }
        const GhostDef*                  findGhost( const hashed_string& id ) const { return _ghostCatalog.find( id ); }
        const GhostRoomDef*              findRoom( const hashed_string& id ) const { return _roomCatalog.find( id ); }
        const GhostDoorDef*              findDoor( const hashed_string& id ) const { return _doorCatalog.find( id ); }
        const GhostFurnitureDef*         findFurniture( const hashed_string& id ) const { return _furnitureCatalog.find( id ); }
        const vector<GhostFurnitureDef>& getFurniture() const { return _furnitureCatalog.getAll(); }
        const vector<GhostBooDef>&       getBoos() const { return _booCatalog.getAll(); }
        int32                            findBooIndex( const hashed_string& id ) const { return _booCatalog.findIndex( id ); }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GhostFlashlightSettings        _flashlight;
        GhostVacuumSettings            _vacuum;
        GameCatalog<GhostDef>          _ghostCatalog;
        GameCatalog<GhostRoomDef>      _roomCatalog;
        GameCatalog<GhostDoorDef>      _doorCatalog;
        GameCatalog<GhostFurnitureDef> _furnitureCatalog;
        GameCatalog<GhostBooDef>       _booCatalog;
    };
} // namespace sw
