/**
 * @file DeckBattle.h
 * @brief 덱 빌딩 전투 코어(문스톤 아일랜드 · 슬레이 더 스파이어 류, 작게) — 뽑을 더미 · 손 · 버린 더미 · 소멸 더미, 턴마다 에너지 · 뽑기,
 *        데이터로 적은 카드 효과(피해 · 방어 · 뽑기 · 에너지)와 차례대로 의도를 실행하는 적 하나입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 효과 종류입니다. */
    enum class DeckBattleEffectKind : uint8
    {
        Damage = 0, ///< 상대 방어를 먼저 깎고 남은 만큼 체력
        Block,      ///< 내 방어(내 다음 턴 시작에 사라진다)
        Draw,
        Energy
    };

    /** @brief 효과 하나입니다. */
    struct DeckBattleEffect
    {
        int32                _amount{ 0 };
        DeckBattleEffectKind _kind{ DeckBattleEffectKind::Damage };
    };
} // namespace sw

namespace sw
{
    /** @brief 카드 정의입니다. */
    struct DeckBattleCardDef
    {
        hashed_string            _id{};
        string                   _name{};
        vector<DeckBattleEffect> _listEffect{};
        int32                    _cost{ 1 };
        uint8                    _bExhaust{ SW_FALSE }; ///< 쓰면 소멸 더미로(이번 전투에서 다시 안 나온다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class DeckBattleCatalog
     * @brief `<DeckBattleCatalog><Card id="strike" name="Strike" cost="1" effects="Damage:6"/></DeckBattleCatalog>` 를 읽습니다.
     *        `effects` 는 쉼표로 나눈 `종류:양`(Damage · Block · Draw · Energy)입니다.
     */
    class SW_GF_API DeckBattleCatalog : public XMLCatalog<DeckBattleCatalog>
    {
        friend class XMLCatalog<DeckBattleCatalog>;

    public:
        DeckBattleCatalog();

        int32 addCard( const DeckBattleCardDef& def ) { return _catalog.add( def ); }

        const DeckBattleCardDef* findCard( const hashed_string& id ) const { return _catalog.find( id ); }
        int32                    findIndex( const hashed_string& id ) const { return _catalog.findIndex( id ); }
        const DeckBattleCardDef& getAt( int32 index ) const { return _catalog.getAt( static_cast<size_t>( index ) ); }
        int32                    getCount() const { return static_cast<int32>( _catalog.getCount() ); }

    private:
        static constexpr const utf8* kXMLRootName = "DeckBattleCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        GameCatalog<DeckBattleCardDef> _catalog;
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 설정입니다. */
    struct DeckBattleSettings
    {
        int32 _playerHp{ 50 };
        int32 _energyPerTurn{ 3 };
        int32 _drawPerTurn{ 5 };
        int32 _handLimit{ 10 }; ///< 넘치게 뽑은 패는 바로 버린 더미로
    };
} // namespace sw

namespace sw
{
    /** @brief 적 하나입니다. 의도는 턴마다 차례대로 돌아갑니다. */
    struct DeckBattleEnemy
    {
        vector<DeckBattleEffect> _listIntent{}; ///< Damage = 공격, Block = 방어(그 밖은 무시)
        int32                    _hp{ 0 };
        int32                    _block{ 0 };
        int32                    _intentIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 단계입니다. */
    enum class DeckBattlePhase : uint8
    {
        PlayerTurn = 0,
        Won,
        Lost
    };

    /**
     * @class DeckBattle
     * @brief 한 전투입니다. 카드 한 장 = `Card`(번호 = 이 전투의 카드 자리), 정의는 `getCardDef` 로 찾습니다. 결정적입니다(씨앗).
     */
    class SW_GF_API DeckBattle
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CDBT" );
        static constexpr uint32 kStateVersion = 1;

        DeckBattle();

        /**
         * @brief 덱(@p listDeckCardId 의 정의 id 들)을 섞어 첫 턴을 엽니다. 카탈로그에 없는 id 는 건너뜁니다.
         * @return 덱이 비면 false 입니다.
         */
        [[nodiscard]] bool initialize( const DeckBattleCatalog* pCatalog, const DeckBattleSettings& settings, const vector<hashed_string>& listDeckCardId,
                                       const DeckBattleEnemy& enemy, uint32 seed );
        /** @brief 손의 @p handIndex 번째 패를 씁니다. 에너지가 모자라거나 내 턴이 아니면 false 입니다. */
        [[nodiscard]] bool playCard( int32 handIndex );
        /** @brief 턴을 마칩니다 — 손을 버리고, 적이 의도를 실행하고, 살아 있으면 다음 턴(방어 초기화 · 에너지 · 뽑기)입니다. */
        void endTurn();

        /** @brief 덱의 정의 id(카드 번호 순) · 네 더미 · 적 · 난수 · 체력 · 방어 · 에너지 · 턴 · 단계를 씁니다. 카탈로그 · 설정은 `initialize` 의 것이라 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 카탈로그에 없는 정의 id 거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        const DeckBattleCardDef* getCardDef( const Card& card ) const;
        const CardPile&          getDrawPile() const { return _drawPile; }
        const CardPile&          getHand() const { return _hand; }
        const CardPile&          getDiscardPile() const { return _discardPile; }
        const CardPile&          getExhaustPile() const { return _exhaustPile; }
        const DeckBattleEnemy&   getEnemy() const { return _enemy; }
        int32                    getPlayerHp() const { return _playerHp; }
        int32                    getPlayerBlock() const { return _playerBlock; }
        int32                    getEnergy() const { return _energy; }
        int32                    getTurn() const { return _turn; }
        DeckBattlePhase          getPhase() const { return _phase; }

    private:
        void startTurn();
        void drawCards( int32 count );
        void applyEffect( const DeckBattleEffect& effect );

        vector<int32>            _listDefIndex; ///< 카드 번호 → 카탈로그 자리
        CardPile                 _drawPile;
        CardPile                 _hand;
        CardPile                 _discardPile;
        CardPile                 _exhaustPile;
        DeckBattleEnemy          _enemy;
        DeckBattleSettings       _settings;
        GameRandom               _random;
        const DeckBattleCatalog* _pCatalog;
        int32                    _playerHp;
        int32                    _playerBlock;
        int32                    _energy;
        int32                    _turn;
        DeckBattlePhase          _phase;
    };
} // namespace sw
