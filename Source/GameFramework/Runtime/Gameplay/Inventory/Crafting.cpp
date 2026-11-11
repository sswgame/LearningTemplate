#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct CraftingInternal
        {
            /** @brief @p pChildName 줄들을 읽습니다. `item` 은 필수, `count` 는 1 이상이고 깨지면 오류와 false 입니다(그 줄은 넣지 않는다). */
            [[nodiscard]] static bool readItems( const XMLNode& node, const utf8* pChildName, ItemStackList& outItems, string_view sourceName )
            {
                bool bValid = true;
                for ( XMLNode child = node.findChild( pChildName ); child; child = child.findNextSibling( pChildName ) )
                {
                    RecipeItemParams line; // item 필수 · count >= 1(적지 않으면 한 개)은 PROPERTY 가 본다
                    if ( XMLFields::readProperties( child, line, sourceName ) )
                        outItems.addItem( line._item, line._count );
                    else
                        bValid = false;
                }
                return bValid;
            }

            /** @brief 결과를 모두 넣을 공간이 있는지 확인합니다. 재료를 뺀 뒤의 상태를 가정하지 않고 보수적으로 판정합니다. */
            static bool hasRoomForOutputs( const Inventory& inventory, const ItemStackList& outputs, int32 count )
            {
                for ( const auto& item : outputs.getItems() )
                {
                    if ( inventory.hasRoomFor( item._itemID, item._count * count ) == false )
                        return false;
                }
                return true;
            }

            static void giveItems( Inventory& inventory, const ItemStackList& items, int32 count )
            {
                for ( const auto& item : items.getItems() )
                {
                    (void)inventory.addItem( item._itemID, item._count * count );
                }
            }

            static void takeItems( Inventory& inventory, const ItemStackList& items, int32 count )
            {
                for ( const auto& item : items.getItems() )
                {
                    (void)inventory.removeItem( item._itemID, item._count * count ); // 재료 수는 canCraft 가 미리 확인했다
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RecipeDef::RecipeDef()
        : _id{}
        , _station{}
        , _inputs{}
        , _tools{}
        , _outputs{}
        , _time{ 0.0f }
        , _requiredLevel{ 0 }
        , _bStartsKnown{ SW_TRUE }
    {
    }

    // ------------------------------------------------------------------------------
    // RecipeCatalog
    // ------------------------------------------------------------------------------

    uint32 RecipeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<RecipeCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&RecipeCatalog::loadRecipe>( "Recipe" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool RecipeCatalog::loadRecipe( const XMLNode& node, string_view sourceName )
    {

        // 적지 않은 칸은 RecipeDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 레시피를 넣지 않는 오류다(자르지 않는다).
        RecipeDef         recipe;
        bool              bValid = XMLFields::readProperties( node, recipe, sourceName );
        const utf8* const pID    = recipe._id.c_str();
        bValid                   = CraftingInternal::readItems( node, "In", recipe._inputs, sourceName ) && bValid;
        bValid                   = CraftingInternal::readItems( node, "Tool", recipe._tools, sourceName ) && bValid;
        bValid                   = CraftingInternal::readItems( node, "Out", recipe._outputs, sourceName ) && bValid;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Crafting.RecipeInvalidFieldsSkipped", "%#: recipe '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        add( recipe );
        return true;
    }

    void RecipeCatalog::findRecipesFor( const hashed_string& itemID, vector<const RecipeDef*>& outListRecipe ) const
    {
        outListRecipe.clear();
        for ( const RecipeDef& recipe : getAll() )
        {
            if ( recipe._outputs.hasItem( itemID ) )
                outListRecipe.push_back( &recipe );
        }
    }

    // ------------------------------------------------------------------------------
    // Crafter
    // ------------------------------------------------------------------------------
    Crafter::Crafter()
        : _pCatalog{ nullptr }
        , _consumeInputs{}
        , _uniqueLearnedRecipe{}
        , _listJob{}
    {
    }

    void Crafter::initialize( const RecipeCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _uniqueLearnedRecipe.clear();
        _listJob.clear();
    }

    bool Crafter::isLearned( const hashed_string& recipeID ) const
    {
        const RecipeDef* pRecipe = _pCatalog != nullptr ? _pCatalog->find( recipeID ) : nullptr;
        return pRecipe != nullptr && ( pRecipe->_bStartsKnown == SW_TRUE || _uniqueLearnedRecipe.find( recipeID ) != _uniqueLearnedRecipe.end() );
    }

    CraftResult Crafter::evaluate( const hashed_string& recipeID, const Inventory& inventory, const hashed_string& station, int32 level, int32 count ) const
    {
        const RecipeDef* pRecipe = _pCatalog != nullptr ? _pCatalog->find( recipeID ) : nullptr;
        if ( pRecipe == nullptr || count <= 0 )
            return CraftResult::UnknownRecipe;
        if ( isLearned( recipeID ) == false )
            return CraftResult::NotLearned;
        if ( pRecipe->_station.empty() == false && pRecipe->_station != station )
            return CraftResult::WrongStation;
        if ( level < pRecipe->_requiredLevel )
            return CraftResult::LevelTooLow;
        if ( inventory.hasItems( pRecipe->_tools ) == false )
            return CraftResult::MissingTools;
        for ( const auto& item : pRecipe->_inputs.getItems() )
        {
            if ( inventory.getItemCount( item._itemID ) < item._count * count )
                return CraftResult::MissingInputs;
        }
        return CraftResult::Ok;
    }

    int32 Crafter::computeMaxCraftCount( const hashed_string& recipeID, const Inventory& inventory, const hashed_string& station, int32 level ) const
    {
        if ( evaluate( recipeID, inventory, station, level, 1 ) != CraftResult::Ok )
            return 0;
        const RecipeDef* pRecipe  = _pCatalog->find( recipeID );
        int32            maxCount = 9999;
        for ( const auto& item : pRecipe->_inputs.getItems() )
        {
            maxCount = MathUtil::min( maxCount, inventory.getItemCount( item._itemID ) / MathUtil::max( 1, item._count ) );
        }
        return maxCount;
    }

    CraftResult Crafter::craft( const hashed_string& recipeID, Inventory& inventory, const hashed_string& station, int32 level, int32 count )
    {
        const CraftResult result = evaluate( recipeID, inventory, station, level, count );
        if ( result != CraftResult::Ok )
            return result;
        const RecipeDef* pRecipe = _pCatalog->find( recipeID );
        if ( _consumeInputs.isBound() )
        {
            // 재료는 다른 곳에서 가져온다. 되돌릴 방법이 없으므로 결과를 넣을 공간을 먼저 확인한다.
            if ( CraftingInternal::hasRoomForOutputs( inventory, pRecipe->_outputs, count ) == false )
                return CraftResult::NoRoom;
            if ( _consumeInputs( pRecipe->_inputs, count ) == false )
                return CraftResult::MissingInputs;
            CraftingInternal::giveItems( inventory, pRecipe->_outputs, count );
            return CraftResult::Ok;
        }
        CraftingInternal::takeItems( inventory, pRecipe->_inputs, count );
        if ( CraftingInternal::hasRoomForOutputs( inventory, pRecipe->_outputs, count ) == false )
        {
            CraftingInternal::giveItems( inventory, pRecipe->_inputs, count ); // 되돌린다
            return CraftResult::NoRoom;
        }
        CraftingInternal::giveItems( inventory, pRecipe->_outputs, count );
        return CraftResult::Ok;
    }

    CraftResult Crafter::enqueue( const hashed_string& recipeID, Inventory& inventory, const hashed_string& station, int32 level, int32 count )
    {
        const CraftResult result = evaluate( recipeID, inventory, station, level, count );
        if ( result != CraftResult::Ok )
            return result;
        const RecipeDef* pRecipe = _pCatalog->find( recipeID );
        if ( _consumeInputs.isBound() )
        {
            if ( _consumeInputs( pRecipe->_inputs, count ) == false )
                return CraftResult::MissingInputs;
        }
        else
        {
            CraftingInternal::takeItems( inventory, pRecipe->_inputs, count );
        }
        CraftJob job;
        job._recipeID  = recipeID;
        job._remaining = pRecipe->_time;
        job._count     = count;
        _listJob.push_back( job );
        return CraftResult::Ok;
    }

    bool Crafter::cancel( size_t index, Inventory& inventory )
    {
        if ( index >= _listJob.size() )
            return false;
        const CraftJob&  job     = _listJob[index];
        const RecipeDef* pRecipe = _pCatalog->find( job._recipeID );
        if ( pRecipe != nullptr )
        {
            if ( CraftingInternal::hasRoomForOutputs( inventory, pRecipe->_inputs, job._count ) == false )
                return false;
            CraftingInternal::giveItems( inventory, pRecipe->_inputs, job._count );
        }
        _listJob.erase( _listJob.begin() + static_cast<std::ptrdiff_t>( index ) );
        return true;
    }

    void Crafter::update( float32 deltaTime, Inventory& inventory, vector<hashed_string>& outListFinished )
    {
        float32 timeLeft = MathUtil::max( 0.0f, deltaTime );
        while ( _listJob.empty() == false )
        {
            CraftJob&        job     = _listJob.front();
            const RecipeDef* pRecipe = _pCatalog != nullptr ? _pCatalog->find( job._recipeID ) : nullptr;
            if ( pRecipe == nullptr )
            {
                _listJob.pop_front();
                continue;
            }
            if ( job._remaining > 0.0f )
            {
                const float32 used = MathUtil::min( timeLeft, job._remaining );
                job._remaining -= used;
                timeLeft -= used;
                if ( job._remaining > 0.0f )
                    return;
            }
            if ( CraftingInternal::hasRoomForOutputs( inventory, pRecipe->_outputs, 1 ) == false )
                return; // 공간이 생길 때까지 기다린다
            CraftingInternal::giveItems( inventory, pRecipe->_outputs, 1 );
            outListFinished.push_back( job._recipeID );
            if ( --job._count > 0 )
                job._remaining = pRecipe->_time; // 남은 횟수가 있으면 다음 것을 이어서 만든다
            else
                _listJob.pop_front();
        }
    }

    void Crafter::serializeState( StateIO& io )
    {
        vector<hashed_string> listLearned( _uniqueLearnedRecipe.begin(), _uniqueLearnedRecipe.end() );
        VectorUtil::sort( listLearned, []( const hashed_string& left, const hashed_string& right )
        {
            return string_view( left.c_str() ) < string_view( right.c_str() );
        } );
        io.list( listLearned );
        if ( io.isReading() )
        {
            _uniqueLearnedRecipe.clear();
            for ( const hashed_string& recipeID : listLearned )
            {
                _uniqueLearnedRecipe.insert( recipeID );
            }
        }
        // 작업마다 이름(4) + 남은 시간(4) + 개수(4)
        uint32 jobCount = static_cast<uint32>( _listJob.size() );
        io.count( 12, jobCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _listJob.clear();
        }
        for ( uint32 index = 0; index < jobCount; ++index )
        {
            CraftJob job = io.isWriting() ? _listJob[index] : CraftJob{};
            io.value( job._recipeID );
            io.value( job._remaining );
            io.value( job._count );
            if ( io.check( job._recipeID.empty() == false && 0.0f <= job._remaining && 0 < job._count ) == false )
                return;
            if ( io.isReading() )
            {
                _listJob.push_back( job );
            }
        }
    }

} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<RecipeCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( RecipeCatalog, RecipeCatalog, SW_GF_API, RecipeCatalog::kAssetType.withLoader( AssetTypeLoader::of<RecipeCatalog>( MemoryTag::Game ) ) );
} // namespace sw
