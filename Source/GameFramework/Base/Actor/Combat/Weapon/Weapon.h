/**
 * @file Weapon.h
 * @brief 총 한 자루의 정의(weapons.xml)와 상태 — 연사 간격 · 탄창 · 재장전 · 탄 퍼짐 · 산탄 · 거리 감쇠 · 탄속입니다.
 * @details 장르 공통입니다 — 1인칭 슈터 · 배틀로얄 · 서부극 · 기체 액션이 같은 정의와 상태를 씁니다(키트끼리는 링크하지 않으므로 기반에 둔다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Combat/Weapon/WeaponMath.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class XmlNode;

    /**
     * @brief 총 한 종류입니다. 각도는 도, 시간은 초, 거리는 m 입니다.
     * @details 퍼짐은 쏠 때마다 `_spreadPerShot` 만큼 커지고(상한 `_maxSpread`), 쏘지 않으면 초당 `_spreadRecovery` 만큼 `_minSpread` 로 돌아옵니다.
     *          피해는 숫자로만 들고 있습니다 — 게임이 어빌리티 시스템의 피해 이펙트(SetByCaller)로 넘깁니다.
     */
    struct WeaponDef
    {
        hashed_string _id{};
        hashed_string _ammoID{}; ///< 예비탄으로 쓰는 아이템 id(인벤토리와 잇는 게임이 본다 — 비면 무기 자체 예비탄)
        string        _name{};
        float32       _fireInterval{ 0.1f }; ///< 발사 사이 최소 간격(s)
        float32       _reloadTime{ 1.8f };
        float32       _damage{ 10.0f }; ///< 한 발(산탄이면 한 알)의 피해
        float32       _range{ 80.0f };
        float32       _minSpread{ 0.5f }; ///< 가만히 쏠 때의 원뿔 반각(도)
        float32       _maxSpread{ 4.0f };
        float32       _spreadPerShot{ 0.6f };
        float32       _spreadRecovery{ 6.0f }; ///< 초당 줄어드는 퍼짐(도)
        float32       _recoilPitch{ 0.6f };    ///< 한 발의 반동(도, 위로)
        float32       _falloffStart{ 0.0f };   ///< 이 거리까지는 피해 그대로(0 = 감쇠 없음)
        float32       _falloffEnd{ 0.0f };     ///< 이 거리부터 `_falloffMinScale`
        float32       _falloffMinScale{ 1.0f };
        float32       _projectileSpeed{ 0.0f };   ///< m/s — 0 이면 히트스캔(광선), 아니면 탄도(`Ballistics`)
        float32       _projectileGravity{ 1.0f }; ///< 중력 배율(탄 낙차 — 0 이면 곧게)
        float32       _headshotMultiplier{ 2.0f };
        int32         _magazineSize{ 30 };
        int32         _maxReserveAmmo{ 180 };
        int32         _pelletCount{ 1 };      ///< 한 번에 나가는 알 수(산탄)
        uint8         _bAutomatic{ SW_TRUE }; ///< 누르고 있으면 계속 쏜다(아니면 누를 때마다 한 발)
    };
} // namespace sw

namespace sw
{
    /** @brief `<WeaponCatalog><Weapon id="rifle" name="Rifle" fireInterval="0.1" .../></WeaponCatalog>` 를 읽습니다(속성 이름 = 필드 이름에서 `_` 를 뺀 것). */
    class SW_GF_API WeaponCatalog : public XmlCatalog<WeaponCatalog>
    {
        friend class XmlCatalog<WeaponCatalog>;

    public:
        WeaponCatalog();

        void addWeapon( const WeaponDef& weapon );

        const WeaponDef*         findWeapon( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<WeaponDef>& getWeapons() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXmlRootName = "WeaponCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<WeaponDef> _catalog; ///< 읽은 순서(무기 바꾸기 순환)
    };
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

    /** @brief 한 번 쏜 결과 — 알마다의 광선입니다. */
    struct WeaponShot
    {
        vector<GameRay> _listRay{};
        float32         _spreadAtFire{ 0.0f }; ///< 이번 발사의 퍼짐(도)
    };
} // namespace sw

namespace sw
{
    /**
     * @class WeaponState
     * @brief 총 한 자루를 들고 있는 상태입니다 — 탄창 · 예비탄 · 연사 쿨다운 · 재장전 · 퍼짐.
     * @details 시간은 `update` 로만 흐릅니다(시험 · 일시정지에서 그대로 멈춘다). 퍼짐 난수는 상태가 들고 있어 씨앗이 같으면 같은 탄이 나갑니다.
     *          연사: 쿨다운이 프레임 안에서 0 을 지나친 몫(늦음)을 다음 발 간격에서 빼므로 연사 속도가 fps 와 무관합니다. `pullTrigger` 한 번에
     *          한 발이고 잇는 몫은 한 간격까지라, fps 가 1 / 간격보다 낮으면 프레임마다 한 발로 떨어집니다(멈춘 프레임 뒤에 몰아 쏘지 않는다 —
     *          CS 의 "한 틱 안의 늦음만 잇는다" 와 같은 규칙). 쉬다가 당긴 첫 발은 쉰 시간을 잇지 않습니다.
     */
    class SW_GF_API WeaponState
    {
    public:
        WeaponState();

        /** @brief 총을 들고 탄창을 채웁니다(예비탄 @p reserveAmmo). */
        void equip( const WeaponDef& weapon, int32 reserveAmmo, uint32 spreadSeed = 1u );
        /** @brief 시간을 흘립니다 — 쿨다운 · 재장전 · 퍼짐 회복. */
        void update( float32 deltaTime );
        /**
         * @brief 방아쇠를 당깁니다(누른 동안 매 프레임). 쏘면 @p outShot 에 알마다 광선을 채웁니다.
         * @param bTriggerJustPressed 이번 프레임에 눌렀는지 — 반자동 총은 이때만 쏜다
         */
        WeaponFireResult pullTrigger( const GameRay& aim, bool bTriggerJustPressed, WeaponShot& outShot );
        /** @brief 재장전을 시작합니다. 이미 가득이거나 예비탄이 없거나 재장전 중이면 false 입니다. */
        [[nodiscard]] bool startReload();
        /** @brief 예비탄을 더합니다(상한까지). */
        void addReserveAmmo( int32 amount );

        const WeaponDef& getDef() const { return _def; }
        int32            getMagazineAmmo() const { return _magazineAmmo; }
        int32            getReserveAmmo() const { return _reserveAmmo; }
        float32          getCurrentSpread() const { return _currentSpread; }
        bool             isReloading() const { return _reload.isActive(); }
        float32          getReloadRemaining() const { return _reload.getRemaining(); }

        /** @brief 무기 정의 id · 난수 · 쿨다운 · 장전 · 퍼짐 · 탄창 · 예비탄을 씁니다. 정의는 `equip` 의 것이라 id 만 싣고, 읽을 때 지금 정의와 id 가 다르면 거절합니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        WeaponDef  _def;
        GameRandom _random;
        Countdown  _cooldown; ///< 다음 발까지 — 발사는 `restart` 로 걸어 늦음을 다음 간격에서 뺀다
        Countdown  _reload;
        float32    _currentSpread;
        int32      _magazineAmmo;
        int32      _reserveAmmo;
    };
} // namespace sw
