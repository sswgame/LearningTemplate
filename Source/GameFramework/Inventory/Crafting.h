/**
 * @file Crafting.h
 * @brief 제작 — 레시피(재료 · 결과 · 도구 · 작업대 · 레벨 · 시간) 카탈로그, 배운 레시피, 즉시 제작과 시간이 걸리는 제작 대기열입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/ItemBag.h"

namespace sw
{
    class Inventory;
    class XmlNode;

    /** @brief 레시피 하나입니다. */
    struct RecipeDef
    {
        hashed_string _id{};
        hashed_string _station{}; ///< 필요한 작업대(비면 어디서나 — 맨손 제작)
        ItemBag       _inputs{};  ///< 써서 없어지는 재료
        ItemBag       _tools{};   ///< 있어야 하지만 남는 것(망치 · 절구)
        ItemBag       _outputs{};
        float32       _time{ 0.0f }; ///< 대기열에서 걸리는 시간 — `Crafter` 는 `update` 에 넘긴 단위(보통 초) 그대로 쓴다. 게임 분으로 흘리는 키트는 분이다
        int32         _requiredLevel{ 0 };
        uint8         _bStartsKnown{ SW_TRUE }; ///< 처음부터 안다(아니면 배워야 한다 — 설계도 · 레시피 책)
    };
} // namespace sw

namespace sw
{
    /** @brief 제작 가능 여부입니다. */
    enum class CraftResult : uint8
    {
        Ok = 0,
        UnknownRecipe,
        NotLearned,
        WrongStation,
        LevelTooLow,
        MissingInputs,
        MissingTools,
        NoRoom ///< 결과를 넣을 자리가 없다
    };

    SW_GF_API const utf8* toString( CraftResult result );

    /**
     * @class RecipeCatalog
     * @brief `<RecipeCatalog><Recipe id="potion" station="Alchemy" time="2" level="1" known="false"><In item="herb" count="2"/>
     *        <Tool item="mortar"/><Out item="potion" count="1"/></Recipe></RecipeCatalog>` 를 읽습니다.
     */
    class SW_GF_API RecipeCatalog
    {
    public:
        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addRecipe( const RecipeDef& recipe ) { (void)_catalog.add( recipe ); }

        const RecipeDef*         findRecipe( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<RecipeDef>& getRecipes() const { return _catalog.getAll(); }
        /** @brief @p itemId 를 만드는 레시피들입니다(제작 화면의 "이걸 만들려면"). */
        void findRecipesFor( const hashed_string& itemId, vector<const RecipeDef*>& outListRecipe ) const;

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<RecipeDef> _catalog{};
    };
} // namespace sw

namespace sw
{
    /** @brief 대기열에서 만드는 중인 것 하나입니다. */
    struct CraftJob
    {
        hashed_string _recipeId{};
        float32       _remaining{ 0.0f };
        int32         _count{ 1 }; ///< 남은 횟수(하나씩 끝난다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class Crafter
     * @brief 한 캐릭터(또는 작업대)의 제작입니다 — 배운 레시피, 즉시 제작, 시간이 걸리는 대기열.
     * @details 대기열은 넣을 때 재료를 모두 거두고(취소하면 돌려준다) 하나가 끝날 때마다 결과를 인벤토리에 넣습니다. 자리가 없으면 그 일은 기다립니다.
     *          레시피 카탈로그는 빌려 씁니다.
     */
    class SW_GF_API Crafter
    {
    public:
        /** @brief 재료를 인벤토리 대신 다른 곳(신선도 묶음 재고 · 원가 장부)에서 거둡니다 — (재료 봉투, 횟수) → 다 거뒀으면 true. */
        using ConsumeInputsDelegate = Delegate<bool( const ItemBag& inputs, int32 count )>;

        Crafter();

        void initialize( const RecipeCatalog* pCatalog );
        /**
         * @brief 재료를 거두는 쪽을 바꿉니다. 묶여 있으면 `craft` · `enqueue` 가 인벤토리에서 빼지 않고 이것을 부릅니다(false 면 `MissingInputs`).
         * @details 묶인 동안 `craft` 는 재료를 빼기 **전에** 결과 자리를 봅니다(되돌릴 길이 없으니). `cancel` 이 돌려주는 재료는 인벤토리로 갑니다.
         */
        void                 setConsumeInputsHandler( const ConsumeInputsDelegate& handler ) { _consumeInputs = handler; }
        const RecipeCatalog* getCatalog() const { return _pCatalog; }
        void                 learnRecipe( const hashed_string& recipeId ) { _uniqueLearnedRecipe.insert( recipeId ); }
        bool                 isLearned( const hashed_string& recipeId ) const;

        /** @brief 지금 @p count 번 만들 수 있는가입니다. */
        CraftResult evaluate( const hashed_string& recipeId, const Inventory& inventory, const hashed_string& station, int32 level, int32 count = 1 ) const;
        /** @brief 재료만 보고 몇 번 만들 수 있는가입니다(레시피 · 작업대 · 레벨 · 도구가 맞지 않으면 0). */
        int32 computeMaxCraftCount( const hashed_string& recipeId, const Inventory& inventory, const hashed_string& station, int32 level ) const;
        /** @brief 바로 만듭니다(재료를 쓰고 결과를 넣는다). */
        CraftResult craft( const hashed_string& recipeId, Inventory& inventory, const hashed_string& station, int32 level, int32 count = 1 );

        /** @brief 대기열에 넣습니다(재료를 지금 거둔다). */
        CraftResult enqueue( const hashed_string& recipeId, Inventory& inventory, const hashed_string& station, int32 level, int32 count = 1 );
        /** @brief 대기열 @p index 를 빼고 남은 횟수의 재료를 돌려줍니다. 돌려줄 자리가 없으면 false 입니다. */
        [[nodiscard]] bool cancel( size_t index, Inventory& inventory );
        /** @brief 시간을 흘립니다. 이번에 끝난 것(레시피 id, 한 번에 하나)을 @p outListFinished 에 더합니다. */
        void update( float32 deltaTime, Inventory& inventory, vector<hashed_string>& outListFinished );

        const deque<CraftJob>& getQueue() const { return _listJob; }

    private:
        const RecipeCatalog*         _pCatalog;
        ConsumeInputsDelegate        _consumeInputs;
        unordered_set<hashed_string> _uniqueLearnedRecipe;
        deque<CraftJob>              _listJob;
    };
} // namespace sw
