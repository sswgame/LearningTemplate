/**
 * @file PartyItemSpawner.h
 * @brief 파티 미니 게임의 무작위 아이템을 생성합니다. 시드를 고정한 난수(`GameRandom`)로 간격, 위치, 종류(가중치)를 정해 원판 위에 놓고, 수명이 지나면 치우며, 가까이 온 사람이 줍습니다.
 * @details 아이템이 **무엇을 하는지는 모른다**. `_effect`(이름)와 `_stats`(이름에서 수치로의 매핑)를 미니 게임이 읽는다(트램펄린 아레나는 `SuperBounce`, `Heavy`,
 *          `Shield` 를 쓴다). 같은 시드와 같은 걸음이면 같은 위치에 같은 아이템이 나온다.
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

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 아이템 정의 하나입니다. */
    struct PartyItemDef
    {
        hashed_string _id{};
        hashed_string _effect{}; ///< 미니 게임이 읽는 효과 이름이다
        StatBlock     _stats{};  ///< 효과의 수치이다(속성 이름 그대로 읽는다. 예: `duration="8" knockbackTaken="0.5"`)
        float32       _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템이 나오는 규칙입니다. */
    REFLECT()
    struct PartyItemSpawnSettings
    {
        REFLECT_BODY();

        float3 _center{};
        PROPERTY( Min = 0.0 )
        float32 _minInterval{ 4.0f };
        PROPERTY( Min = "_minInterval" )
        float32 _maxInterval{ 8.0f };
        PROPERTY( Min = 0.0 )
        float32 _lifetime{ 10.0f }; ///< 0 이면 주울 때까지 남는다
        PROPERTY( Name = "radius", Min = 0.0 )
        float32 _spawnRadius{ 6.0f }; ///< 가운데에서 이 반지름의 원판 안에 고르게 나온다
        PROPERTY()
        float32 _height{ 1.5f }; ///< 아이템이 놓이는 높이
        PROPERTY( Min = 1 )
        int32 _maxActive{ 2 };
    };
} // namespace sw

namespace sw
{
    /** @brief 놓여 있는 아이템 하나입니다. */
    struct PartyItemInstance
    {
        float3        _position{};
        hashed_string _itemID{};
        float32       _age{ 0.0f };
        int32         _serial{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템에 일어난 일을 알리는 이벤트입니다. */
    struct PartyItemEvent
    {
        enum class Kind : uint8
        {
            Spawned = 0,
            Expired,
            PickedUp ///< `_player` 가 주운 사람이다
        };
        hashed_string _itemID{};
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
    class SW_KIT_API PartyItemSpawner : public XMLCatalog<PartyItemSpawner>
    {
        friend class XMLCatalog<PartyItemSpawner>;

    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "PISP" );
        static constexpr uint32 kStateVersion = 1;

        PartyItemSpawner();

        /** @brief 규칙과 시드를 정하고 놓인 아이템을 비웁니다(정의는 남깁니다). 첫 아이템은 최소 간격 뒤에 나옵니다. */
        void initialize( const PartyItemSpawnSettings& settings, uint32 seed );
        void addItem( const PartyItemDef& def ) { (void)_catalog.add( def ); }

        void update( float32 deltaTime );
        /** @brief @p position 에서 @p radius 안의 가장 가까운 아이템을 @p player 가 줍습니다. 없으면 false 를 반환합니다. */
        [[nodiscard]] bool tryPickUp( const float3& position, float32 radius, int32 player, PartyItemInstance& outItem );
        void               drainEvents( vector<PartyItemEvent>& outListEvent );
        /** @brief 쌓인 이벤트를 꺼내지 않고 버립니다. 이벤트를 쓰지 않는 쪽에서 받을 목록을 만들어 복사하지 않게 하려는 함수입니다. */
        void discardEvents() { _eventBuffer.clear(); }

        /** @brief 놓인 아이템(위치, 정의 id, 나이, 번호), 난수, 다음 아이템까지 남은 시간, 다음 번호를 씁니다. 정의와 규칙은 저장하지 않고, 이벤트는 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 모르는 아이템 id 이거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        const PartyItemDef*              findItem( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<PartyItemInstance>& getInstances() const { return _listInstance; }
        const PartyItemSpawnSettings&    getSettings() const { return _settings; }
        float32                          getSpawnTimer() const { return _spawnTimer.getRemaining(); }

    private:
        static constexpr const utf8* kXMLRootName = "PartyItems"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        const PartyItemDef*          pickWeighted();
        void                         spawnOne();
        float32                      rollInterval();

        GameCatalog<PartyItemDef>   _catalog;
        vector<PartyItemInstance>   _listInstance;
        EventBuffer<PartyItemEvent> _eventBuffer;
        PartyItemSpawnSettings      _settings;
        GameRandom                  _random;
        Countdown                   _spawnTimer;
        int32                       _nextSerial;
    };
} // namespace sw
