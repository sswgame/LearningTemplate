#include "pch.h"

#include "GameFramework/Base/Inventory/Crafting.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Inventory/Inventory.h"

namespace sw
{
    namespace
    {
        struct CraftingInternal
        {
            static void readItems( const XmlNode& node, const utf8* pChildName, ItemBag& outBag )
            {
                for ( XmlNode child = node.findChild( pChildName ); child; child = child.findNextSibling( pChildName ) )
                {
                    const utf8* pItem = child.findAttribute( "item" );
                    if ( pItem != nullptr )
                        outBag.addItem( hashed_string( pItem ), MathUtil::max( 1, child.getAttributeInt( "count", 1 ) ) );
                }
            }

            /** @brief 결과를 모두 넣을 자리가 있는가 — 재료를 뺀 뒤를 가정하지 않는다(보수적). */
            static bool hasRoomForOutputs( const Inventory& inventory, const ItemBag& outputs, int32 count )
            {
                for ( const auto& item : outputs.getItems() )
                {
                    if ( inventory.hasRoomFor( item.first, item.second * count ) == false )
                        return false;
                }
                return true;
            }

            static void giveItems( Inventory& inventory, const ItemBag& items, int32 count )
            {
                for ( const auto& item : items.getItems() )
                    (void)inventory.addItem( item.first, item.second * count );
            }

            static void takeItems( Inventory& inventory, const ItemBag& items, int32 count )
            {
                for ( const auto& item : items.getItems() )
                    (void)inventory.removeItem( item.first, item.second * count );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( CraftResult result )
    {
        switch ( result )
        {
            case CraftResult::Ok:
                return "Ok";
            case CraftResult::UnknownRecipe:
                return "UnknownRecipe";
            case CraftResult::NotLearned:
                return "NotLearned";
            case CraftResult::WrongStation:
                return "WrongStation";
            case CraftResult::LevelTooLow:
                return "LevelTooLow";
            case CraftResult::MissingInputs:
                return "MissingInputs";
            case CraftResult::MissingTools:
                return "MissingTools";
            case CraftResult::NoRoom:
                return "NoRoom";
        }
        return "Unknown";
    }

    // ------------------------------------------------------------------------------
    // RecipeCatalog
    // ------------------------------------------------------------------------------

    uint32 RecipeCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Recipe" ); node; node = node.findNextSibling( "Recipe" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            RecipeDef recipe;
            recipe._id            = hashed_string( pId );
            const utf8* pStation  = node.findAttribute( "station" );
            recipe._station       = pStation != nullptr ? hashed_string( pStation ) : hashed_string{};
            recipe._time          = MathUtil::max( 0.0f, node.getAttributeFloat( "time", recipe._time ) );
            recipe._requiredLevel = node.getAttributeInt( "level", recipe._requiredLevel );
            recipe._bStartsKnown  = node.getAttributeBool( "known", true ) ? SW_TRUE : SW_FALSE;
            CraftingInternal::readItems( node, "In", recipe._inputs );
            CraftingInternal::readItems( node, "Tool", recipe._tools );
            CraftingInternal::readItems( node, "Out", recipe._outputs );
            addRecipe( recipe );
            ++loadedCount;
        }
        return loadedCount;
    }

    void RecipeCatalog::findRecipesFor( const hashed_string& itemId, vector<const RecipeDef*>& outListRecipe ) const
    {
        outListRecipe.clear();
        for ( const RecipeDef& recipe : _catalog.getAll() )
        {
            if ( recipe._outputs.hasItem( itemId ) )
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

    bool Crafter::isLearned( const hashed_string& recipeId ) const
    {
        const RecipeDef* pRecipe = _pCatalog != nullptr ? _pCatalog->findRecipe( recipeId ) : nullptr;
        return pRecipe != nullptr && ( pRecipe->_bStartsKnown || _uniqueLearnedRecipe.find( recipeId ) != _uniqueLearnedRecipe.end() );
    }

    CraftResult Crafter::evaluate( const hashed_string& recipeId, const Inventory& inventory, const hashed_string& station, int32 level, int32 count ) const
    {
        const RecipeDef* pRecipe = _pCatalog != nullptr ? _pCatalog->findRecipe( recipeId ) : nullptr;
        if ( pRecipe == nullptr || count <= 0 )
            return CraftResult::UnknownRecipe;
        if ( isLearned( recipeId ) == false )
            return CraftResult::NotLearned;
        if ( pRecipe->_station.empty() == false && pRecipe->_station != station )
            return CraftResult::WrongStation;
        if ( level < pRecipe->_requiredLevel )
            return CraftResult::LevelTooLow;
        if ( inventory.hasItems( pRecipe->_tools ) == false )
            return CraftResult::MissingTools;
        for ( const auto& item : pRecipe->_inputs.getItems() )
        {
            if ( inventory.getItemCount( item.first ) < item.second * count )
                return CraftResult::MissingInputs;
        }
        return CraftResult::Ok;
    }

    int32 Crafter::computeMaxCraftCount( const hashed_string& recipeId, const Inventory& inventory, const hashed_string& station, int32 level ) const
    {
        if ( evaluate( recipeId, inventory, station, level, 1 ) != CraftResult::Ok )
            return 0;
        const RecipeDef* pRecipe  = _pCatalog->findRecipe( recipeId );
        int32            maxCount = 9999;
        for ( const auto& item : pRecipe->_inputs.getItems() )
            maxCount = MathUtil::min( maxCount, inventory.getItemCount( item.first ) / MathUtil::max( 1, item.second ) );
        return maxCount;
    }

    CraftResult Crafter::craft( const hashed_string& recipeId, Inventory& inventory, const hashed_string& station, int32 level, int32 count )
    {
        const CraftResult result = evaluate( recipeId, inventory, station, level, count );
        if ( result != CraftResult::Ok )
            return result;
        const RecipeDef* pRecipe = _pCatalog->findRecipe( recipeId );
        if ( _consumeInputs.isBound() )
        {
            // 재료는 다른 곳에서 거둔다 — 되돌릴 길이 없으니 결과 자리를 먼저 본다.
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

    CraftResult Crafter::enqueue( const hashed_string& recipeId, Inventory& inventory, const hashed_string& station, int32 level, int32 count )
    {
        const CraftResult result = evaluate( recipeId, inventory, station, level, count );
        if ( result != CraftResult::Ok )
            return result;
        const RecipeDef* pRecipe = _pCatalog->findRecipe( recipeId );
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
        job._recipeId  = recipeId;
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
        const RecipeDef* pRecipe = _pCatalog->findRecipe( job._recipeId );
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
            const RecipeDef* pRecipe = _pCatalog != nullptr ? _pCatalog->findRecipe( job._recipeId ) : nullptr;
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
                return; // 자리가 날 때까지 기다린다
            CraftingInternal::giveItems( inventory, pRecipe->_outputs, 1 );
            outListFinished.push_back( job._recipeId );
            if ( --job._count > 0 )
                job._remaining = pRecipe->_time; // 남은 시간으로 다음 것을 이어 만든다
            else
                _listJob.pop_front();
        }
    }
} // namespace sw
