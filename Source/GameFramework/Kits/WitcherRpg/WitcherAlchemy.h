/**
 * @file WitcherAlchemy.h
 * @brief 연금술 — 레시피로 물약 · 변이 혼합물 · 오일 · 폭탄 만들기(기반 `Crafter`), 사용 횟수와 명상으로 보충(술 소모), 독성(최대치 · 서서히 감소 ·
 *        변이 혼합물은 효과 동안 묶임), 칼에 바른 오일의 적중 횟수입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/Crafting.h"

namespace sw
{
    enum class WitcherAlchemyKind : uint8;

    struct WitcherAlchemyDef;

    class Inventory;
    class WitcherCatalog;

    /** @brief 연금술 물건 쓰기 결과입니다. */
    enum class WitcherUseResult : uint8
    {
        Ok = 0,
        UnknownItem,
        WrongKind,      ///< 마시는 것이 아니다 · 칼에 바르는 것이 아니다 · 던지는 것이 아니다
        NotInInventory, ///< 만든 적이 없다
        NoCharges,      ///< 다 썼다 — 명상으로 보충한다
        TooToxic,       ///< 마시면 최대 독성을 넘는다
        AlreadyActive   ///< 같은 변이 혼합물이 이미 돈다
    };

    SW_GF_API const utf8* toString( WitcherUseResult result );

    /** @brief 지금 도는 효과 하나입니다. */
    struct WitcherActiveEffect
    {
        hashed_string _itemId{};
        float32       _remaining{ 0.0f };
        float32       _lockedToxicity{ 0.0f }; ///< 변이 혼합물 — 효과가 끝날 때까지 줄지 않는 몫
    };

    /**
     * @class WitcherAlchemy
     * @brief 위쳐 한 명의 연금술입니다. 물건 하나는 한 번 만들면 인벤토리에 남고(아이템 하나) 사용 횟수만 줄어듭니다 — 명상하면 술 하나로 모든 물건이
     *        가득 찹니다. 독성은 묶이지 않은 몫이 초당 일정량 줄고, 변이 혼합물의 몫은 효과가 끝날 때 풀립니다.
     */
    class SW_GF_API WitcherAlchemy
    {
    public:
        WitcherAlchemy();

        void initialize( const WitcherCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog );
        /** @brief 레시피를 만듭니다(기반 `Crafter::craft`). 결과가 연금술 물건이면 그 사용 횟수를 가득 채웁니다. */
        CraftResult brew( const hashed_string& recipeId, Inventory& inoutInventory, const hashed_string& station, int32 level );
        /** @brief 마실 수 있는가입니다(물약 · 변이 혼합물). */
        WitcherUseResult evaluateDrink( const hashed_string& itemId, const Inventory& inventory ) const;
        /** @brief 마십니다 — 사용 횟수 하나 · 독성 · 효과. */
        WitcherUseResult drink( const hashed_string& itemId, const Inventory& inventory );
        /** @brief 칼에 오일을 바릅니다(앞의 오일은 지운다). */
        WitcherUseResult applyOil( const hashed_string& itemId, const Inventory& inventory );
        /** @brief 폭탄을 던집니다. 성공하면 @p outElement 가 폭탄의 속성입니다. */
        WitcherUseResult throwBomb( const hashed_string& itemId, const Inventory& inventory, hashed_string& outElement );
        /** @brief 칼이 맞혔습니다 — 바른 오일의 속성을 돌려주고(없으면 빈 이름) 적중 횟수를 하나 씁니다. */
        hashed_string consumeOilHit();
        /** @brief 명상합니다 — 술 하나를 쓰고 가진 모든 연금술 물건의 사용 횟수를 채웁니다. 술이 없으면 false 입니다. */
        [[nodiscard]] bool meditate( Inventory& inoutInventory );
        void               update( float32 deltaTime );

        float32                            getToxicity() const;
        float32                            getMaxToxicity() const;
        int32                              getCharges( const hashed_string& itemId ) const;
        bool                               isEffectActive( const hashed_string& itemId ) const;
        hashed_string                      getOilElement() const;
        int32                              getOilHits() const { return _oilHits; }
        const vector<WitcherActiveEffect>& getActiveEffects() const { return _listEffect; }
        Crafter&                           getCrafter() { return _crafter; }

    private:
        struct ChargeEntry
        {
            hashed_string _itemId{};
            int32         _charges{ 0 };
        };

        const WitcherAlchemyDef* findDef( const hashed_string& itemId ) const;
        ChargeEntry*             findCharge( const hashed_string& itemId );
        WitcherUseResult         evaluateUse( const hashed_string& itemId, const Inventory& inventory, WitcherAlchemyKind kind ) const;
        void                     refill( const hashed_string& itemId );
        void                     spendCharge( const hashed_string& itemId );

        Crafter                     _crafter;
        vector<ChargeEntry>         _listCharge;
        vector<WitcherActiveEffect> _listEffect;
        const WitcherCatalog*       _pCatalog;
        const RecipeCatalog*        _pRecipeCatalog;
        hashed_string               _oilId;
        float32                     _floatingToxicity; ///< 묶이지 않은 독성(서서히 준다)
        int32                       _oilHits;
    };
} // namespace sw
