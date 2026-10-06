/**
 * @file MetroCharmLoadout.h
 * @brief 부적 · 문양 장착 — 가진 부적, 슬롯(노치) 비용, 넘겨 끼기(빈 슬롯이 하나라도 있으면 한 번 — 대신 받는 피해 배율), 낀 부적 능력치의 합입니다.
 * @details 할로우 나이트의 부적 · 블라스퍼머스의 묵주 구슬 · 더 라스트 페이스의 문양이 같은 규칙입니다. 능력치는 기반 `StatBlock` 이라 이름은 게임이 정합니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class MetroidvaniaCatalog;
    class StatBlock;

    /** @brief 끼기 · 벗기 결과입니다. */
    enum class MetroCharmResult : uint8
    {
        Equipped = 0,
        Overcharmed, ///< 슬롯을 넘겨 꼈다(받는 피해 배율이 붙는다)
        Unequipped,
        UnknownCharm,
        NotOwned,
        AlreadyEquipped,
        NotEquipped,
        NotEnoughNotches
    };

    /**
     * @class MetroCharmLoadout
     * @brief 캐릭터 하나의 부적입니다. 넘겨 끼기는 규칙 `_bAllowOvercharm` 일 때, 끼기 전에 쓴 슬롯이 전체보다 적을 때만 됩니다.
     */
    class SW_GF_API MetroCharmLoadout
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MCHM" );
        static constexpr uint32 kStateVersion = 1;

        MetroCharmLoadout();

        /** @brief 카탈로그 규칙의 처음 슬롯 수로 시작합니다(가진 것 · 낀 것은 비운다). */
        void initialize( const MetroidvaniaCatalog* pCatalog );

        /** @brief 부적을 얻습니다. 새로 얻었으면 true 입니다. */
        bool grantCharm( const hashed_string& charmId );
        /** @brief 슬롯을 늘립니다(슬롯 조각). */
        void addNotches( int32 count ) { _notchCount += count > 0 ? count : 0; }

        MetroCharmResult equip( const hashed_string& charmId );
        MetroCharmResult unequip( const hashed_string& charmId );

        /** @brief 낀 부적 능력치를 @p outStats 에 더합니다(먼저 비우지 않는다). */
        void  mergeStats( StatBlock& outStats ) const;
        int32 computeUsedNotches() const;
        bool  isOvercharmed() const { return computeUsedNotches() > _notchCount; }
        /** @brief 넘겨 끼었으면 규칙의 배율, 아니면 1 입니다. */
        float32 computeDamageTakenScale() const;
        bool    isOwned( const hashed_string& charmId ) const;
        bool    isEquipped( const hashed_string& charmId ) const;

        int32                        getNotchCount() const { return _notchCount; }
        const vector<hashed_string>& getEquipped() const { return _listEquipped; }

        /** @brief 가진 부적 · 낀 부적(낀 순서) · 슬롯 수를 씁니다. 카탈로그는 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 카탈로그에 없는 부적 · 가지지 않은 부적을 꼈거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        static bool contains( const vector<hashed_string>& listId, const hashed_string& id );

        const MetroidvaniaCatalog* _pCatalog;
        vector<hashed_string>      _listOwned;
        vector<hashed_string>      _listEquipped; ///< 낀 순서
        int32                      _notchCount;
    };
} // namespace sw
