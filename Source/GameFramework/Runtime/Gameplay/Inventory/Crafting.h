/**
 * @file Crafting.h
 * @brief 제작 시스템입니다. 레시피 카탈로그(재료, 결과, 도구, 작업대, 레벨, 시간), 배운 레시피, 즉시 제작, 시간이 걸리는 제작 대기열을 담습니다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    class Archive;
    class Inventory;
    class XMLNode;

    /** @brief 레시피 하나입니다. */
    REFLECT()
    struct SW_GF_API RecipeDef
    {
        REFLECT_BODY();

        RecipeDef();

        PROPERTY( Required )
        hashed_string _id;
        PROPERTY()
        hashed_string _station; ///< 필요한 작업대(비어 있으면 어디서나 만들 수 있는 맨손 제작)
        ItemStackList _inputs;  ///< 써서 없어지는 재료(`<In item count/>`)
        ItemStackList _tools;   ///< 있어야 하지만 소모되지 않는 것(망치, 절구 — `<Tool …/>`)
        ItemStackList _outputs; ///< `<Out …/>`
        PROPERTY( Min = 0.0 )
        float32 _time; ///< 대기열에서 걸리는 시간. `Crafter` 는 `update` 에 넘긴 단위(보통 초)를 그대로 쓴다. 게임 시간을 분 단위로 진행하는 키트에서는 분이다
        PROPERTY( Name = "level" )
        int32 _requiredLevel;
        PROPERTY( Name = "known" )
        uint8 _bStartsKnown : 1; ///< 처음부터 아는 레시피인지 여부(아니면 설계도나 레시피 책으로 배워야 한다)
    };

    /** @brief 레시피의 물건 줄(`<In|Tool|Out item count/>`)입니다. */
    REFLECT()
    struct SW_GF_API RecipeItemParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _item{};
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 제작할 수 있는지, 없다면 왜 없는지 나타냅니다. */
    ENUM()
    enum class CraftResult : uint8
    {
        Ok = 0,
        UnknownRecipe,
        NotLearned,
        WrongStation,
        LevelTooLow,
        MissingInputs,
        MissingTools,
        NoRoom ///< 결과를 넣을 공간이 없다
    };

    /**
     * @class RecipeCatalog
     * @brief `<RecipeCatalog><Recipe id="potion" station="Alchemy" time="2" level="1" known="false"><In item="herb" count="2"/>
     *        <Tool item="mortar"/><Out item="potion" count="1"/></Recipe></RecipeCatalog>` 를 읽습니다.
     */
    class SW_GF_API RecipeCatalog : public GameCatalog<RecipeDef, XMLCatalog<RecipeCatalog>>
    {
        friend class XMLCatalog<RecipeCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<RecipeCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".recipes.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `Crafting.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "RecipeCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief @p itemID 를 만드는 레시피 목록입니다. 제작 화면의 "이걸 만들려면" 에 씁니다. */
        void findRecipesFor( const hashed_string& itemID, vector<const RecipeDef*>& outListRecipe ) const;

    private:
        static constexpr const utf8* kXMLRootName = "RecipeCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadRecipe( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<RecipeCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, RecipeCatalog );
} // namespace sw

namespace sw
{
    /** @brief 대기열에서 만드는 중인 작업 하나입니다. */
    struct CraftJob
    {
        hashed_string _recipeID{};
        float32       _remaining{ 0.0f };
        int32         _count{ 1 }; ///< 남은 횟수(하나씩 끝남)
        /** @brief 남은 횟수마다 인벤토리에서 꺼낸 재료 조각(개체 상태째, 앞이 먼저 끝난다). 취소는 이것을 그대로 돌려준다. 재료를 바깥(`ConsumeInputsDelegate`)에서 가져왔으면 비어 있다. */
        vector<vector<InventorySlot>> _listUnitInput{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class Crafter
     * @brief 한 캐릭터나 작업대의 제작을 담당합니다. 배운 레시피, 즉시 제작, 시간이 걸리는 대기열을 관리합니다.
     * @details 대기열은 작업을 넣을 때 재료를 모두 가져가고(취소하면 돌려준다) 하나가 끝날 때마다 결과를 인벤토리에 넣는다. 공간이 없으면 그 작업은 기다린다.
     *          레시피 카탈로그는 소유하지 않고 참조만 한다.
     */
    class SW_GF_API Crafter
    {
    public:
        static constexpr uint32 kStateTag     = FourCCUtil::make( "CRFT" );
        static constexpr uint32 kStateVersion = 2; ///< 2: 작업마다 꺼낸 재료 조각을 쓴다(판 1 은 머리표가 없어 거절한다)

        /** @brief 재료를 인벤토리 대신 다른 곳(신선도별 재고, 원가 장부)에서 가져오는 함수입니다. 인자는 (재료 목록, 횟수)이고, 재료를 모두 가져왔으면 true 를 반환합니다. */
        using ConsumeInputsDelegate = Delegate<bool( const ItemStackList& inputs, int32 count )>;

        Crafter();

        void initialize( const RecipeCatalog* pCatalog );
        /**
         * @brief 재료를 가져오는 쪽을 바꿉니다. 지정해 두면 `craft` 와 `enqueue` 가 인벤토리에서 빼지 않고 이 함수를 부릅니다. 이 함수가 false 를 반환하면 `MissingInputs` 입니다.
         * @details 지정한 동안 `craft` 는 재료를 빼기 **전에** 결과를 넣을 공간을 확인한다. 재료를 되돌릴 방법이 없기 때문이다. `cancel` 이 돌려주는 재료는 인벤토리로 간다.
         */
        void                 setConsumeInputsHandler( const ConsumeInputsDelegate& handler ) { _consumeInputs = handler; }
        const RecipeCatalog* getCatalog() const { return _pCatalog; }
        void                 learnRecipe( const hashed_string& recipeID ) { _uniqueLearnedRecipe.insert( recipeID ); }
        bool                 isLearned( const hashed_string& recipeID ) const;

        /** @brief 지금 @p count 번 만들 수 있는지 판정합니다. */
        CraftResult evaluate( const hashed_string& recipeID, const Inventory& inventory, const hashed_string& station, int32 level, int32 count = 1 ) const;
        /** @brief 재료만 보고 몇 번 만들 수 있는지 계산합니다. 레시피, 작업대, 레벨, 도구가 맞지 않으면 0 입니다. */
        int32 computeMaxCraftCount( const hashed_string& recipeID, const Inventory& inventory, const hashed_string& station, int32 level ) const;
        /** @brief 바로 만듭니다. 재료를 소모하고 결과를 넣습니다. */
        CraftResult craft( const hashed_string& recipeID, Inventory& inventory, const hashed_string& station, int32 level, int32 count = 1 );

        /** @brief 대기열에 넣습니다. 재료는 지금 가져갑니다. */
        CraftResult enqueue( const hashed_string& recipeID, Inventory& inventory, const hashed_string& station, int32 level, int32 count = 1 );
        /** @brief 대기열 @p index 를 빼고 남은 횟수의 재료를 돌려줍니다. 돌려줄 공간이 없으면 false 입니다. */
        [[nodiscard]] bool cancel( size_t index, Inventory& inventory );
        /** @brief 시간을 진행합니다. 이번에 끝난 작업(레시피 id, 한 번에 하나)을 @p outListFinished 에 추가합니다. */
        void update( float32 deltaTime, Inventory& inventory, vector<hashed_string>& outListFinished );

        const deque<CraftJob>& getQueue() const { return _listJob; }

        /**
         * @brief 배운 레시피(이름 순)와 작업 대기열(레시피 id, 남은 시간, 개수)을 씁니다. 카탈로그와 재료 처리기는 저장하지 않습니다.
         *        `writeState` 가 쓴 바이트로 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        const RecipeCatalog*         _pCatalog;
        ConsumeInputsDelegate        _consumeInputs;
        unordered_set<hashed_string> _uniqueLearnedRecipe;
        deque<CraftJob>              _listJob;
    };
} // namespace sw
