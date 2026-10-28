/**
 * @file DeckBattle.h
 * @brief 덱 빌딩 전투의 코어를 구현합니다. 문스톤 아일랜드나 슬레이 더 스파이어 류를 작게 줄인 것으로, 뽑을 더미, 손, 버린 더미, 소멸 더미를 두고 턴마다 에너지와 뽑기를 처리합니다.
 *        카드 효과(피해, 방어, 뽑기, 에너지)는 데이터로 적고, 적 하나가 의도를 차례대로 실행합니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 효과의 종류입니다. */
    enum class DeckBattleEffectKind : uint8
    {
        Damage = 0, ///< 상대의 방어를 먼저 깎고, 남은 만큼 체력을 깎는다
        Block,      ///< 내 방어를 올린다(내 다음 턴이 시작되면 사라진다)
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
    REFLECT()
    struct DeckBattleCardDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string                   _name{};
        vector<DeckBattleEffect> _listEffect{};
        PROPERTY( Min = 0 )
        int32 _cost{ 1 };
        PROPERTY()
        bool _bExhaust{ false }; ///< 쓰면 소멸 더미로 간다(이번 전투에서 다시 나오지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class DeckBattleCatalog
     * @brief `<DeckBattleCatalog><Card id="strike" name="Strike" cost="1" effects="Damage:6"/></DeckBattleCatalog>` 를 읽습니다.
     *        `effects` 는 쉼표로 나눈 `종류:양`(Damage, Block, Draw, Energy)입니다.
     */
    class SW_KIT_API DeckBattleCatalog : public XMLCatalog<DeckBattleCatalog>, public GameCatalog<DeckBattleCardDef>
    {
        friend class XMLCatalog<DeckBattleCatalog>;

    public:
        DeckBattleCatalog();

    private:
        static constexpr const utf8* kXMLRootName = "DeckBattleCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 설정입니다. */
    struct DeckBattleSettings
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
    struct DeckBattleEnemy
    {
        vector<DeckBattleEffect> _listIntent{}; ///< Damage 는 공격, Block 은 방어이다(그 밖의 종류는 무시한다)
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
     * @brief 한 전투를 진행합니다. 카드 한 장은 `Card` 이고 번호는 이 전투 안에서의 카드 index 입니다. 정의는 `getCardDef` 로 찾습니다. 시드를 쓰므로 결과가 결정적입니다.
     */
    class SW_KIT_API DeckBattle
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CDBT" );
        static constexpr uint32 kStateVersion = 1;

        DeckBattle();

        /**
         * @brief 덱(@p listDeckCardID 의 정의 id 목록)을 섞어 첫 턴을 엽니다. 카탈로그에 없는 id 는 건너뜁니다.
         * @return 덱이 비면 false 를 반환한다.
         */
        [[nodiscard]] bool initialize( const DeckBattleCatalog* pCatalog, const DeckBattleSettings& settings, const vector<hashed_string>& listDeckCardID,
                                       const DeckBattleEnemy& enemy, uint32 seed );
        /** @brief 손의 @p handIndex 번째 패를 씁니다. 에너지가 모자라거나 내 턴이 아니면 false 를 반환합니다. */
        [[nodiscard]] bool playCard( int32 handIndex );
        /** @brief 턴을 마칩니다. 손을 버리고, 적이 의도를 실행하고, 살아 있으면 다음 턴을 시작합니다(방어 초기화, 에너지, 뽑기). */
        void endTurn();

        /** @brief 덱의 정의 id(카드 번호 순), 더미 넷, 적, 난수, 체력, 방어, 에너지, 턴, 단계를 씁니다. 카탈로그와 설정은 `initialize` 에서 받으므로 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 카탈로그에 없는 정의 id 이거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        const DeckBattleCardDef* getCardDef( const Card& card ) const;
        const CardPile&          getDrawPile() const { return _drawPile; }
        const CardPile&          getHand() const { return _hand; }
        const CardPile&          getDiscardPile() const { return _discardPile; }
        const CardPile&          getExhaustPile() const { return _exhaustPile; }
        const DeckBattleEnemy&   getEnemy() const { return _enemy; }
        int32                    getPlayerHP() const { return _playerHP; }
        int32                    getPlayerBlock() const { return _playerBlock; }
        int32                    getEnergy() const { return _energy; }
        int32                    getTurn() const { return _turn; }
        DeckBattlePhase          getPhase() const { return _phase; }

    private:
        void startTurn();
        void drawCards( int32 count );
        void applyEffect( const DeckBattleEffect& effect );

        vector<int32>            _listDefIndex; ///< 카드 번호에서 카탈로그 index 로 가는 매핑이다
        CardPile                 _drawPile;
        CardPile                 _hand;
        CardPile                 _discardPile;
        CardPile                 _exhaustPile;
        DeckBattleEnemy          _enemy;
        DeckBattleSettings       _settings;
        GameRandom               _random;
        const DeckBattleCatalog* _pCatalog;
        int32                    _playerHP;
        int32                    _playerBlock;
        int32                    _energy;
        int32                    _turn;
        DeckBattlePhase          _phase;
    };
} // namespace sw
