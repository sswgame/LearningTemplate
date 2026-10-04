/**
 * @file PartyItemSpawner.h
 * @brief 파티 미니 게임의 무작위 아이템 — 씨앗 고정 난수(`GameRandom`)로 간격 · 자리 · 종류(가중치)를 정해 원판 위에 놓고, 수명이 지나면 치우고, 가까이 온 사람이 줍습니다.
 * @details 아이템이 **무엇을 하는지는 모릅니다** — `_effect`(이름)와 `_stats`(이름 → 수치)를 미니 게임이 읽습니다(트램펄린 아레나는 `SuperBounce` · `Heavy` ·
 *          `Shield`). 같은 씨앗 · 같은 걸음이면 같은 자리에 같은 것이 나옵니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    class XmlNode;

    /** @brief 아이템 정의 하나입니다. */
    struct PartyItemDef
    {
        hashed_string _id{};
        hashed_string _effect{}; ///< 미니 게임이 읽는 효과 이름
        StatBlock     _stats{};  ///< 효과의 수치(속성 이름 그대로 — `duration="8" knockbackTaken="0.5"`)
        float32       _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 나오는 규칙입니다. */
    struct PartyItemSpawnSettings
    {
        float3  _center{};
        float32 _minInterval{ 4.0f };
        float32 _maxInterval{ 8.0f };
        float32 _lifetime{ 10.0f };   ///< 0 = 주울 때까지
        float32 _spawnRadius{ 6.0f }; ///< 가운데에서 이 반지름 원판 안(고르게)
        float32 _height{ 1.5f };      ///< 놓이는 높이
        int32   _maxActive{ 2 };
    };
} // namespace sw

namespace sw
{
    /** @brief 놓여 있는 아이템 하나입니다. */
    struct PartyItemInstance
    {
        float3        _position{};
        hashed_string _itemId{};
        float32       _age{ 0.0f };
        int32         _serial{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템에 생긴 일입니다. */
    struct PartyItemEvent
    {
        enum class Kind : uint8
        {
            Spawned = 0,
            Expired,
            PickedUp ///< _player = 주운 사람
        };
        hashed_string _itemId{};
        int32         _serial{ 0 };
        int32         _player{ -1 };
        Kind          _kind{ Kind::Spawned };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PartyItemSpawner
     * @brief `<PartyItems minInterval="4" maxInterval="8" lifetime="10" radius="6" height="1.5" maxActive="2"><Item id="spring" effect="SuperBounce" weight="3"/>
     *        <Item id="anvil" effect="Heavy" weight="2" duration="8" knockbackTaken="0.5" knockbackDealt="1.5"/></PartyItems>` 를 읽습니다.
     */
    class SW_GF_API PartyItemSpawner
    {
    public:
        PartyItemSpawner();

        /** @brief 규칙과 씨앗을 두고 놓인 것을 비웁니다(정의는 남긴다). 첫 아이템은 최소 간격 뒤입니다. */
        void               initialize( const PartyItemSpawnSettings& settings, uint32 seed );
        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addItem( const PartyItemDef& def ) { (void)_catalog.add( def ); }

        void update( float32 deltaTime );
        /** @brief @p position 에서 @p radius 안의 가장 가까운 아이템을 @p player 가 줍습니다. 없으면 false 입니다. */
        [[nodiscard]] bool tryPickUp( const float3& position, float32 radius, int32 player, PartyItemInstance& outItem );
        void               drainEvents( vector<PartyItemEvent>& outListEvent );

        const PartyItemDef*              findItem( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<PartyItemInstance>& getInstances() const { return _listInstance; }
        const PartyItemSpawnSettings&    getSettings() const { return _settings; }
        float32                          getSpawnTimer() const { return _spawnTimer; }

    private:
        uint32              loadRoot( const XmlNode& root, string_view sourceName );
        const PartyItemDef* pickWeighted();
        void                spawnOne();
        float32             rollInterval();

        GameCatalog<PartyItemDef> _catalog;
        vector<PartyItemInstance> _listInstance;
        vector<PartyItemEvent>    _listEvent;
        PartyItemSpawnSettings    _settings;
        GameRandom                _random;
        float32                   _spawnTimer;
        int32                     _nextSerial;
    };
} // namespace sw
