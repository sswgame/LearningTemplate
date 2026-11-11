/**
 * @file Weapon.h
 * @brief 총 한 자루의 정의(weapons.xml)와 상태를 정의합니다.
 * @details 무기는 쏘는 법(연사 간격, 탄창, 재장전, 퍼짐, 반동, 알 수)만 가진다. 날아가는 것(피해, 사거리, 탄속, 감쇠)은 탄 정의(`ProjectileDef`, projectiles.xml)이고,
 *          무기는 받는 탄을 태그 조건(`accepts`)으로, 기본 탄을 id(`projectile`)로, 탄 값에 얹는 배율을 `<Modifier>` 로 적는다. 장전한 조합은 `ResolvedShot` 하나로 든다.
 *          장르와 무관하며 1인칭 슈터, 배틀로얄, 서부극, 기체 액션이 같은 정의와 상태를 쓴다. 키트끼리는 링크하지 않으므로 기반에 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/WeaponMath.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /**
     * @brief 총 한 종류입니다. 각도는 도, 시간은 초입니다.
     * @details 퍼짐은 쏠 때마다 `_spreadPerShot` 만큼 커지고(상한은 `_maxSpread`), 쏘지 않으면 초당 `_spreadRecovery` 만큼 `_minSpread` 로 돌아온다.
     *          탄 값은 탄 정의 → 이 무기의 `<Modifier>` → 부착물 수정자 순서로 얹는다(`ResolvedShot::resolve`).
     */
    REFLECT()
    struct SW_GF_API WeaponDef
    {
        REFLECT_BODY();

        WeaponDef();

        PROPERTY( Required )
        hashed_string _id;
        PROPERTY( Name = "ammo" )
        hashed_string _ammoID; ///< 예비탄으로 쓰는 아이템 id. 인벤토리와 연결하는 게임이 읽는다. 비면 무기 자체 예비탄을 쓴다. 세이브의 탄 id 는 이 칸을 그대로 잇는다
        PROPERTY( Name = "item" )
        hashed_string _itemID; ///< 이 무기를 나타내는 장비 아이템 id. 표시 이름과 외형은 그 아이템이 정한다(이름을 무기 표에 또 적지 않는다). 아이템 표를 쓰지 않는 게임은 비운다
        PROPERTY( Name = "projectile", Required )
        hashed_string _projectileID; ///< 기본으로 장전하는 탄(`ProjectileCatalog` 의 id)
        PROPERTY()
        string _accepts; ///< 받는 탄의 태그 조건식(`Ammo.Rifle || Ammo.Pistol`). 그냥 이름은 태그다. 비면 모든 탄을 받는다
        PROPERTY()
        string _name; ///< 로그용 이름. 데이터에 `name` 이 없으면 id 다
        PROPERTY( Min = 0.01, Units = s )
        float32 _fireInterval; ///< 발사 사이 최소 간격(s)
        PROPERTY( Min = 0.0, Units = s )
        float32 _reloadTime;
        PROPERTY( Min = 0.0 )
        float32 _minSpread; ///< 가만히 쏠 때의 원뿔 반각(도)
        PROPERTY( Min = "_minSpread" )
        float32 _maxSpread;
        PROPERTY( Min = 0.0 )
        float32 _spreadPerShot;
        PROPERTY( Min = 0.0 )
        float32 _spreadRecovery; ///< 초당 줄어드는 퍼짐(도)
        PROPERTY()
        float32 _recoilPitch; ///< 한 발의 반동(도, 위로)
        PROPERTY( Min = 1 )
        int32 _magazineSize;
        PROPERTY( Min = 0 )
        int32 _maxReserveAmmo;
        PROPERTY( Min = 1 )
        int32 _pelletCount; ///< 한 번에 나가는 알 수(산탄)
        PROPERTY()
        uint8 _bAutomatic : 1; ///< 누르고 있으면 계속 쏜다(아니면 누를 때마다 한 발)
        PROPERTY()
        uint8 _bTwoHanded : 1; ///< 두 손으로 든다(소총 · 산탄총) — 몸이 두 손 조준 · 사격 자세를 고른다(언리얼 Lyra 의 무기별 애니메이션 층과 같은 자리)

        ConditionExpr           _acceptsExpr;  ///< `_accepts` 를 읽을 때 컴파일한 것
        vector<ShotModifierDef> _listModifier; ///< `<Modifier>` 줄 — 탄 값에 이 무기가 얹는 배율 · 합

        /** @brief 받는 탄 조건(`_accepts`)을 컴파일합니다. 틀린 식이면 오류를 남기고 false 입니다. 코드로 짓는 정의(테스트)도 `_accepts` 를 고친 뒤 부릅니다. */
        [[nodiscard]] bool compileAccepts( string_view sourceName );
        /** @brief 이 무기가 탄 @p projectile 을 받는지 나타냅니다(탄 태그에 대한 `_accepts`). */
        bool canAccept( const ProjectileDef& projectile ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 무기가 가리키는 아이템 id 하나입니다(`findUnknownItems` 의 결과). */
    struct WeaponItemReference
    {
        hashed_string _weaponID{};
        hashed_string _itemID{};
        const utf8*   _pField{ "" }; ///< 가리킨 속성 이름("item" 또는 "ammo")
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `<WeaponCatalog><Weapon id="rifle" projectile="rifle_round" accepts="Ammo.Rifle" fireInterval="0.1" ...><Modifier field="Damage" value="1.1"/></Weapon></WeaponCatalog>` 를 읽습니다.
     * @details 속성 이름은 필드 이름에서 `_` 를 뺀 것입니다. 항목은 `_base` 로 다른 무기를 물려받고 `<Modifier>` 는 `field` 가 열쇠다(`datacataloginheritance.xml`).
     */
    class SW_GF_API WeaponCatalog : public GameCatalog<WeaponDef, XMLCatalog<WeaponCatalog>>
    {
        friend class XMLCatalog<WeaponCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_ASSET_TYPE` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "weapons";

        WeaponCatalog();

        /**
         * @brief 무기가 가리키는 아이템 id(`item` · `ammo`) 가운데 @p items 에 없는 것을 @p outListUnknown 에 담고, 모두 있으면 true 를 반환합니다.
         * @details 두 필드는 이름 없이 id 만 들고 있어 오타가 로드에서는 드러나지 않는다. 아이템 표(`find( id )` 가 null 을 돌려주는 카탈로그)를 읽은 뒤 게임이 한 번 부르고,
         *          비어 있지 않은 목록이면 오류로 남기고 멈춘다. 아이템 표는 위층(Gameplay)이라 형식을 템플릿으로 받는다.
         */
        template <typename TItemCatalog>
        [[nodiscard]] bool findUnknownItems( const TItemCatalog& items, vector<WeaponItemReference>& outListUnknown ) const
        {
            outListUnknown.clear();
            for ( const WeaponDef& weapon : getAll() )
            {
                if ( weapon._itemID.empty() == false && items.find( weapon._itemID ) == nullptr )
                    outListUnknown.push_back( WeaponItemReference{ weapon._id, weapon._itemID, "item" } );
                if ( weapon._ammoID.empty() == false && items.find( weapon._ammoID ) == nullptr )
                    outListUnknown.push_back( WeaponItemReference{ weapon._id, weapon._ammoID, "ammo" } );
            }
            return outListUnknown.empty();
        }
        /**
         * @brief 무기마다 기본 탄(`projectile`)이 @p projectiles 에 있고 그 무기가 받는 탄(`accepts`)인지 봅니다. 아니면 무기마다 오류를 남기고 false 입니다.
         * @details 두 카탈로그는 따로 읽으므로 둘 다 읽은 뒤 게임이 한 번 부른다. 틀리면 게임을 띄우지 않는다(쏘려 해도 말없이 탄이 없는 무기를 만들지 않는다).
         */
        [[nodiscard]] bool checkProjectiles( const ProjectileCatalog& projectiles ) const;

    private:
        static constexpr const utf8* kXMLRootName = "WeaponCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadWeapon( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<WeaponCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, WeaponCatalog );
} // namespace sw

namespace sw
{
    /** @brief 방아쇠를 당긴 결과입니다. */
    enum class WeaponFireResult : uint8
    {
        Fired = 0,     ///< 쐈다(`WeaponShot` 에 광선이 있다)
        Cooling,       ///< 연사 간격이 아직 안 지났다
        Reloading,     ///< 재장전 중
        EmptyMagazine, ///< 탄창이 비었다(예비탄이 있으면 재장전을 시작한다)
        OutOfAmmo,     ///< 탄창도 예비탄도 없다
        SemiAutoHeld   ///< 반자동인데 방아쇠를 떼지 않았다
    };

    /** @brief 한 번 쏜 결과입니다. 알마다의 광선을 담습니다. */
    struct WeaponShot
    {
        vector<Ray> _listRay{};
        float32     _spreadAtFire{ 0.0f }; ///< 이번 발사의 퍼짐(도)
    };
} // namespace sw

namespace sw
{
    /**
     * @class WeaponState
     * @brief 총 한 자루의 상태입니다. 탄창, 예비탄, 연사 쿨다운, 재장전, 퍼짐, 장전한 탄의 조합(`ResolvedShot`)을 담습니다.
     * @details 시간은 `update` 로만 흐르므로 테스트나 일시정지에서는 그대로 멈춘다.
     *          퍼짐 난수는 상태가 보관하므로, 시드가 같으면 같은 탄이 나간다.
     *          연사에서는 쿨다운이 프레임 안에서 0 을 지나친 시간(늦음)을 다음 발 간격에서 빼므로 연사 속도가 fps 와 무관하다.
     *          `pullTrigger` 한 번에 한 발이고, 다음 발로 넘기는 시간은 한 간격까지이다. 그래서 fps 가 1 / 간격보다 낮으면 프레임마다 한 발로 떨어진다.
     *          멈춘 프레임 뒤에 몰아서 쏘지 않으며, CS 의 "한 틱 안의 늦음만 이어 준다"와 같은 규칙이다.
     *          쉬다가 당긴 첫 발은 쉰 시간을 이어 주지 않는다.
     */
    class SW_GF_API WeaponState
    {
    public:
        WeaponState();

        /**
         * @brief 총을 들고 탄 @p projectile 을 장전해 탄창을 채웁니다. 예비탄은 @p reserveAmmo 입니다.
         * @return 무기가 그 탄을 받지 않으면(`WeaponDef::canAccept`) 오류를 남기고 false 이며 상태는 그대로입니다.
         */
        [[nodiscard]] bool equip( const WeaponDef& weapon, const ProjectileDef& projectile, int32 reserveAmmo, uint32 spreadSeed = 1u );
        /**
         * @brief 탄 · 부착물을 바꿔 조합을 다시 계산합니다(탄창 · 쿨다운은 그대로). 장전 · 부착물이 바뀔 때 한 번 부르고, 쏠 때는 든 결과를 씁니다.
         * @return 무기가 그 탄을 받지 않으면 오류를 남기고 false 이며 조합은 그대로입니다.
         */
        [[nodiscard]] bool loadProjectile( const ProjectileDef& projectile, const vector<ShotModifierDef>& listAttachment );
        /** @brief 시간을 흘립니다. 쿨다운, 재장전, 퍼짐 회복이 진행됩니다. */
        void update( float32 deltaTime );
        /**
         * @brief 방아쇠를 당깁니다(누른 동안 매 프레임). 쏘면 @p outShot 에 알마다 광선을 채웁니다.
         * @param bTriggerJustPressed 이번 프레임에 눌렀는지 나타낸다. 반자동 총은 이때만 쏜다.
         */
        WeaponFireResult pullTrigger( const Ray& aim, bool bTriggerJustPressed, WeaponShot& outShot );
        /** @brief 재장전을 시작합니다. 이미 가득이거나 예비탄이 없거나 재장전 중이면 false 입니다. */
        [[nodiscard]] bool startReload();
        /** @brief 예비탄을 상한까지 더합니다. */
        void addReserveAmmo( int32 amount );

        const WeaponDef&    getDef() const { return _def; }
        const ResolvedShot& getShot() const { return _shot; }
        int32               getMagazineAmmo() const { return _magazineAmmo; }
        int32               getReserveAmmo() const { return _reserveAmmo; }
        float32             getCurrentSpread() const { return _currentSpread; }
        bool                isReloading() const { return _reload.isActive(); }
        float32             getReloadRemaining() const { return _reload.getRemaining(); }

        /**
         * @brief 무기 정의 id, 장전한 탄 id, 난수, 쿨다운, 장전, 퍼짐, 탄창, 예비탄을 씁니다. 정의와 조합은 `equip` · `loadProjectile` 의 것이라 id 만 싣고, 읽을 때 지금 것과 id 가 다르면 거절합니다.
         *        `writeState` 의 바이트로 상태를 바꿉니다. 바이트가 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        WeaponDef    _def;
        ResolvedShot _shot;
        GameRandom   _random;
        Countdown    _cooldown; ///< 다음 발까지 남은 시간. 발사는 `restart` 로 시작해 늦음을 다음 간격에서 뺀다
        Countdown    _reload;
        float32      _currentSpread;
        int32        _magazineAmmo;
        int32        _reserveAmmo;
    };
} // namespace sw
