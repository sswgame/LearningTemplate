/**
 * @file CardBattle.h
 * @brief 덱 빌딩 전투의 코어를 구현합니다. 문스톤 아일랜드나 슬레이 더 스파이어 류를 작게 줄인 것으로, 뽑을 더미, 손, 버린 더미, 소멸 더미를 두고 턴마다 에너지와 뽑기를 처리합니다.
 *        카드 효과(피해, 방어, 뽑기, 에너지)는 데이터로 적고, 적 하나가 의도를 차례대로 실행합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 효과의 종류입니다. */
    enum class CardBattleEffectKind : uint8
    {
        Damage = 0, ///< 상대의 방어를 먼저 깎고, 남은 만큼 체력을 깎는다
        Block,      ///< 내 방어를 올린다(내 다음 턴이 시작되면 사라진다)
        Draw,
        Energy
    };

    /** @brief 효과 하나입니다. */
    struct CardBattleEffect
    {
        int32                _amount{ 0 };
        CardBattleEffectKind _kind{ CardBattleEffectKind::Damage };
    };
} // namespace sw

namespace sw
{
    /** @brief 카드 정의입니다. */
    REFLECT()
    struct CardBattleCardDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string                   _name{};
        vector<CardBattleEffect> _listEffect{};
        PROPERTY( Min = 0 )
        int32 _cost{ 1 };
        PROPERTY()
        bool _bExhaust{ false }; ///< 쓰면 소멸 더미로 간다(이번 전투에서 다시 나오지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class CardBattleCatalog
     * @brief `<CardBattleCatalog><Card id="strike" name="Strike" cost="1" effects="Damage:6"/></CardBattleCatalog>` 를 읽습니다.
     *        `effects` 는 쉼표로 나눈 `종류:양`(Damage, Block, Draw, Energy)입니다.
     */
    class SW_KIT_API CardBattleCatalog : public GameCatalog<CardBattleCardDef, XMLCatalog<CardBattleCatalog>>
    {
        friend class XMLCatalog<CardBattleCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<CardBattleCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".deckbattle.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `CardBattle.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "CardBattleCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        CardBattleCatalog();

    private:
        static constexpr const utf8* kXMLRootName = "CardBattleCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadCard( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<CardBattleCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, CardBattleCatalog );
} // namespace sw

namespace sw
{
    /** @brief 전투 설정입니다. */
    struct CardBattleSettings
    {
        int32 _playerHP{ 50 };
        int32 _energyPerTurn{ 3 };
        int32 _drawPerTurn{ 5 };
        int32 _handLimit{ 10 }; ///< 손이 넘치게 뽑은 패는 바로 버린 더미로 간다
    };
} // namespace sw

namespace sw
{
    /** @brief 적 하나입니다. 의도는 턴마다 차례대로 돌아갑니다. */
    struct CardBattleEnemy
    {
        vector<CardBattleEffect> _listIntent{}; ///< Damage 는 공격, Block 은 방어이다(그 밖의 종류는 무시한다)
        int32                    _hp{ 0 };
        int32                    _block{ 0 };
        int32                    _intentIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 단계입니다. */
    enum class CardBattlePhase : uint8
    {
        PlayerTurn = 0,
        Won,
        Lost
    };

    /**
     * @class CardBattle
     * @brief 한 전투를 진행합니다. 카드 한 장은 `Card` 이고 번호는 이 전투 안에서의 카드 index 입니다. 정의는 `getCardDef` 로 찾습니다. 시드를 쓰므로 결과가 결정적입니다.
     */
    class SW_KIT_API CardBattle
    {
    public:
        static constexpr uint32 kStateTag     = FourCCUtil::make( "CDBT" );
        static constexpr uint32 kStateVersion = 1;

        CardBattle();

        /**
         * @brief 덱(@p listDeckCardID 의 정의 id 목록)을 섞어 첫 턴을 엽니다. 카탈로그에 없는 id 는 건너뜁니다.
         * @return 덱이 비면 false 를 반환한다.
         */
        [[nodiscard]] bool initialize( const CardBattleCatalog* pCatalog, const CardBattleSettings& settings, const vector<hashed_string>& listDeckCardID,
                                       const CardBattleEnemy& enemy, uint32 seed );
        /** @brief 손의 @p handIndex 번째 패를 씁니다. 에너지가 모자라거나 내 턴이 아니면 false 를 반환합니다. */
        [[nodiscard]] bool playCard( int32 handIndex );
        /** @brief 턴을 마칩니다. 손을 버리고, 적이 의도를 실행하고, 살아 있으면 다음 턴을 시작합니다(방어 초기화, 에너지, 뽑기). */
        void endTurn();

        /**
         * @brief 덱의 정의 id(카드 번호 순), 더미 넷, 적, 난수, 체력, 방어, 에너지, 턴, 단계를 씁니다. 카탈로그와 설정은 `initialize` 에서 받으므로 저장하지 않습니다.
         *        `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 카탈로그에 없는 정의 id, 음수이거나 최대를 넘는 값, 덱과 맞지 않는 더미(빠지거나 겹친 카드)이거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

        const CardBattleCardDef* getCardDef( const Card& card ) const;
        const CardPile&          getDrawPile() const { return _drawPile; }
        const CardPile&          getHand() const { return _hand; }
        const CardPile&          getDiscardPile() const { return _discardPile; }
        const CardPile&          getExhaustPile() const { return _exhaustPile; }
        const CardBattleEnemy&   getEnemy() const { return _enemy; }
        int32                    getPlayerHP() const { return _playerHP; }
        int32                    getPlayerBlock() const { return _playerBlock; }
        int32                    getEnergy() const { return _energy; }
        int32                    getTurn() const { return _turn; }
        CardBattlePhase          getPhase() const { return _phase; }

    private:
        void startTurn();
        void drawCards( int32 count );
        void applyEffect( const CardBattleEffect& effect );

    private:
        vector<int32>            _listDefIndex; ///< 카드 번호에서 카탈로그 index 로 가는 매핑이다
        CardPile                 _drawPile;
        CardPile                 _hand;
        CardPile                 _discardPile;
        CardPile                 _exhaustPile;
        CardBattleEnemy          _enemy;
        CardBattleSettings       _settings;
        GameRandom               _random;
        const CardBattleCatalog* _pCatalog;
        int32                    _playerHP;
        int32                    _playerBlock;
        int32                    _energy;
        int32                    _turn;
        CardBattlePhase          _phase;
    };
} // namespace sw
