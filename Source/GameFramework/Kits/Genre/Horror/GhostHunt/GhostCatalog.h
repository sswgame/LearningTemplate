/**
 * @file GhostCatalog.h
 * @brief 유령 사냥(루이지 맨션 장르)의 데이터를 담습니다. 손전등과 청소기 수치, 유령 종류, 저택의 방(유령, 열쇠 보상, 불 플래그), 열쇠 문, 가구, 부(Boo)를 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 손전등 수치입니다. 각은 원뿔의 절반(도)입니다. */
    REFLECT()
    struct GhostFlashlightDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1 )
        float32 _range{ 8.0f };
        PROPERTY( Name = "angle", Min = 1.0, Max = 89.0 )
        float32 _halfAngle{ 25.0f };
        PROPERTY( Min = 0.1 )
        float32 _strobeRange{ 10.0f };
        PROPERTY( Name = "strobeAngle", Min = 1.0, Max = 89.0 )
        float32 _strobeHalfAngle{ 40.0f };
        PROPERTY( Name = "strobeCharge", Min = 0.0 )
        float32 _strobeChargeTime{ 1.0f }; ///< 이만큼 모아야 스트로브가 터진다(덜 모으면 헛번쩍한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 청소기 수치입니다. */
    REFLECT()
    struct GhostVacuumDef
    {
        REFLECT_BODY();

        vector<float32> _listStagePower{ 10.0f }; ///< 강화 단계마다의 초당 피해이다(0단계부터 · `stages="10 16 24"`)
        PROPERTY( Min = 0.1 )
        float32 _range{ 4.0f }; ///< 기절한 유령을 빨아들이기 시작할 수 있는 거리
        PROPERTY( Min = -1.0, Max = 1.0 )
        float32 _alignThreshold{ 0.7f }; ///< 당기는 방향과 도망 반대 방향의 내적이 이 값 이상이면 "반대로 당긴다" 고 본다
        PROPERTY( Min = 0.0 )
        float32 _alignBonus{ 1.5f }; ///< 반대로 당길 때의 피해 배율
        PROPERTY( Name = "surgeFill", Min = 0.01 )
        float32 _surgeFillTime{ 2.0f }; ///< 반대로 이만큼 당기면 순간 흡입(서지) 게이지가 찬다
        PROPERTY( Min = 0.0 )
        float32 _surgeDamage{ 40.0f };
        PROPERTY( Min = 0.0 )
        float32 _dragSpeed{ 2.0f }; ///< 유령이 플레이어를 끄는 초당 거리이다(유령의 `_pull` 배)
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _dragReduction{ 0.75f }; ///< 반대로 당기면 끌림이 이 비율만큼 줄어든다
    };
} // namespace sw

namespace sw
{
    /** @brief 유령 한 종류입니다. 시간 단위는 초입니다. */
    REFLECT()
    struct GhostDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 1.0 )
        float32 _hp{ 40.0f };
        PROPERTY( Min = 0.0 )
        float32 _hideTime{ 2.0f }; ///< 숨어 있다가 나타날 때까지의 시간
        PROPERTY( Min = 0.0 )
        float32 _appearTime{ 1.5f }; ///< 나타나 있다가 공격할 때까지의 시간
        PROPERTY( Min = 0.05 )
        float32 _attackTime{ 1.0f }; ///< 공격 동작의 시간이다(이 끝에 맞는다). 이 동안 심장이 드러나 손전등에 기절한다
        PROPERTY( Min = 0.0 )
        float32 _attackDamage{ 8.0f };
        PROPERTY( Min = 0.1 )
        float32 _stunTime{ 3.0f };
        PROPERTY( Min = 0.0 )
        float32 _pull{ 1.0f }; ///< 끄는 힘 배율
        PROPERTY( Min = 0.1 )
        float32 _fleeInterval{ 1.0f }; ///< 흡입 중 도망 방향을 바꾸는 간격
        PROPERTY( Min = 0 )
        int32 _coins{ 10 }; ///< 잡으면 떨어뜨리는 동전
        PROPERTY()
        bool _bStrobeOnly{ false }; ///< 보통 손전등으로는 기절하지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 저택의 방 하나입니다(그래프는 `AreaGraph` 가 가집니다). */
    REFLECT()
    struct GhostRoomDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "key" )
        hashed_string _keyReward{}; ///< 불이 켜지면 주는 열쇠
        PROPERTY( Required )
        hashed_string         _lightFlag{}; ///< 불이 켜지면 1 이 되는 플래그(`lightFlag`, 필수)
        vector<hashed_string> _listGhost{};
    };
} // namespace sw

namespace sw
{
    /** @brief 열쇠 문입니다. 열리면 `_flag` 가 1 이 되고, 그래프 연결의 `requires` 가 그 값을 봅니다. */
    REFLECT()
    struct GhostDoorDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _key{};
        PROPERTY( Required )
        hashed_string _flag{};
    };
} // namespace sw

namespace sw
{
    /** @brief 가구를 뒤지는 방법입니다. */
    enum class GhostSearchMode : uint8
    {
        Vacuum = 0, ///< 빨아들이기이다(커튼, 식탁보)
        Shake       ///< 흔들기와 살피기이다(서랍, 옷장)
    };

    /** @brief 가구 하나입니다. */
    REFLECT()
    struct GhostFurnitureDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _room{};
        PROPERTY( Name = "loot" )
        hashed_string _lootTable{};
        bool          _bVacuum{ true };
        bool          _bShake{ true };
    };
} // namespace sw

namespace sw
{
    /** @brief 부(Boo) 하나입니다. 가구에 숨어 있다가 들키면 `_escapeTime` 안에 잡지 못할 때 다른 방으로 달아납니다. */
    REFLECT()
    struct GhostBooDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _room{};
        PROPERTY()
        hashed_string _furniture{};
        PROPERTY( Min = 1.0 )
        float32 _hp{ 30.0f };
        PROPERTY( Min = 0.1 )
        float32 _escapeTime{ 5.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief `<GhostHunt>` 루트 속성의 스키마입니다. */
    REFLECT()
    struct GhostHuntRootDef
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _currency{ "Coins" }; ///< 잡은 유령의 동전을 넣는 지갑 통화
    };
} // namespace sw

namespace sw
{
    /**
     * @class GhostCatalog
     * @brief `<GhostHunt><Flashlight range="8" angle="25" strobeRange="10" strobeAngle="40" strobeCharge="1"/>
     *        <Vacuum range="4" stages="10 16 24" alignThreshold="0.7" alignBonus="1.5" surgeFill="2" surgeDamage="40" dragSpeed="2" dragReduction="0.75"/>
     *        <Ghost id="goob" hp="40" .../><Room id="foyer" ghosts="goob,goob" key="parlorKey" lightFlag="lit.foyer"/><Door id="parlorDoor" key="parlorKey" flag="door.parlor"/>
     *        <Furniture id="dresser" room="foyer" loot="dresserLoot" search="Shake"/><Boo id="booA" room="foyer" furniture="dresser" hp="30" escapeTime="5"/></GhostHunt>`
     *        를 읽습니다.
     */
    class SW_KIT_API GhostCatalog : public XMLCatalog<GhostCatalog>
    {
        friend class XMLCatalog<GhostCatalog>;

    public:
        GhostCatalog();

        const GhostFlashlightDef& getFlashlight() const { return _flashlight; }
        /** @brief 잡은 유령의 동전을 넣는 지갑 통화를 반환합니다(루트의 `currency` 이고 기본은 "Coins" 입니다). */
        const hashed_string&             getCurrency() const { return _currency; }
        const GhostVacuumDef&            getVacuum() const { return _vacuum; }
        const GhostDef*                  findGhost( const hashed_string& id ) const { return _ghostCatalog.find( id ); }
        const GhostRoomDef*              findRoom( const hashed_string& id ) const { return _roomCatalog.find( id ); }
        const GhostDoorDef*              findDoor( const hashed_string& id ) const { return _doorCatalog.find( id ); }
        const GhostFurnitureDef*         findFurniture( const hashed_string& id ) const { return _furnitureCatalog.find( id ); }
        const vector<GhostFurnitureDef>& getFurniture() const { return _furnitureCatalog.getAll(); }
        const vector<GhostBooDef>&       getBoos() const { return _booCatalog.getAll(); }
        int32                            findBooIndex( const hashed_string& id ) const { return _booCatalog.findIndex( id ); }

    private:
        static constexpr const utf8* kXMLRootName = "GhostHunt"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 이름에 맞는 스키마로 읽습니다. 오류면 그 원소를 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool loadVacuum( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadRoom( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadFurniture( const XMLNode& node, string_view sourceName );

        GhostFlashlightDef             _flashlight;
        GhostVacuumDef                 _vacuum;
        GameCatalog<GhostDef>          _ghostCatalog;
        GameCatalog<GhostRoomDef>      _roomCatalog;
        GameCatalog<GhostDoorDef>      _doorCatalog;
        GameCatalog<GhostFurnitureDef> _furnitureCatalog;
        GameCatalog<GhostBooDef>       _booCatalog;
        hashed_string                  _currency;
    };
} // namespace sw
