#include "pch.h"

#include "Games/Playground/FeatureDemo/CraftingDemoComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

#include "Games/Playground/FeatureDemo/FeatureDemoCommandUtil.h"

namespace sw
{
    namespace
    {
        struct CraftingDemoComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "FDCR" };

            static bool runEnqueue( const vector<string>& listArgument, string& outReply )
            {
                int32      count     = 1;
                const bool bBadCount = listArgument.size() == 3 && StringUtil::parseInt( listArgument[2], count ) == false;
                if ( listArgument.size() < 2 || listArgument.size() > 3 || bBadCount )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                if ( pDemo->enqueue( hashed_string( listArgument[1] ), count ).has_value() == false )
                {
                    outReply = "the crafting demo has no character with a bag";
                    return false;
                }
                FeatureDemoCommandUtil::reply( outReply, "craft %# x%#: %#", listArgument[1], count, pDemo->getLastResult().c_str() ); // 거절도 명령의 답이다
                return true;
            }

            static bool runCancel( const vector<string>& listArgument, string& outReply )
            {
                int32 index = 0;
                if ( listArgument.size() != 2 || StringUtil::parseInt( listArgument[1], index ) == false )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                outReply = pDemo->cancel( index ) ? "cancelled" : "not cancelled (no such job or no room for its inputs)";
                return true;
            }

            static bool runLearn( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->learn( hashed_string( listArgument[1] ) );
                FeatureDemoCommandUtil::reply( outReply, "learned %#", listArgument[1] );
                return true;
            }

            static bool runLevel( const vector<string>& listArgument, string& outReply )
            {
                int32 level = 0;
                if ( listArgument.size() != 2 || StringUtil::parseInt( listArgument[1], level ) == false )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->setLevel( level );
                FeatureDemoCommandUtil::reply( outReply, "crafting level %#", level );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( CraftEnqueue, "craft.enqueue", "craft.enqueue <demo object> <recipe> [count]", "Queue a recipe at a crafting demo", &CraftingDemoComponentInternal::runEnqueue );
    SW_DEV_COMMAND( CraftCancel, "craft.cancel", "craft.cancel <demo object> <job index>", "Cancel a queued job of a crafting demo and return its inputs", &CraftingDemoComponentInternal::runCancel );
    SW_DEV_COMMAND( CraftLearn, "craft.learn", "craft.learn <demo object> <recipe>", "Learn a recipe at a crafting demo", &CraftingDemoComponentInternal::runLearn );
    SW_DEV_COMMAND( CraftLevel, "craft.level", "craft.level <demo object> <level>", "Set the crafting level of a crafting demo", &CraftingDemoComponentInternal::runLevel );

    CraftingDemoComponent::CraftingDemoComponent()
        : _recipePath{}
        , _station{}
        , _startLevel{ 1 }
        , _characterName{}
        , _level{ 0 }
        , _finishedCount{ 0 }
        , _queueCount{ 0 }
        , _lastResult{ "none" }
        , _pRecipes{}
        , _crafter{}
    {
    }

    CraftingDemoComponent::~CraftingDemoComponent() = default;

    bool CraftingDemoComponent::startGame()
    {
        if ( acquireRequired( _recipePath, _pRecipes ) == false )
            return false;
        _crafter.initialize( _pRecipes.get() );
        _level = _startLevel;
        return true;
    }

    Inventory* CraftingDemoComponent::findBag() const
    {
        GameObject*                  pObject    = engine::findActiveObject( _characterName );
        CharacterEquipmentComponent* pCharacter = pObject != nullptr ? pObject->getComponent<CharacterEquipmentComponent>() : nullptr;
        return pCharacter != nullptr && pCharacter->isStarted() ? &pCharacter->getBagForTrade() : nullptr;
    }

    optional<CraftResult> CraftingDemoComponent::enqueue( const hashed_string& recipeID, int32 count )
    {
        Inventory* pBag = findBag();
        if ( pBag == nullptr )
            return nullopt;
        const CraftResult result = _crafter.enqueue( recipeID, *pBag, _station, _level, count );
        _lastResult              = hashed_string( getEnumName( result ) );
        _queueCount              = static_cast<int32>( _crafter.getQueue().size() );
        return result;
    }

    bool CraftingDemoComponent::cancel( int32 index )
    {
        Inventory* pBag = findBag();
        if ( pBag == nullptr || index < 0 )
            return false;
        const bool bCancelled = _crafter.cancel( static_cast<size_t>( index ), *pBag );
        _queueCount           = static_cast<int32>( _crafter.getQueue().size() );
        return bCancelled;
    }

    void CraftingDemoComponent::updateGame( float32 deltaTime )
    {
        Inventory* pBag = findBag();
        if ( pBag == nullptr )
            return;
        vector<hashed_string> listFinished;
        _crafter.update( deltaTime, *pBag, listFinished );
        _finishedCount += static_cast<int32>( listFinished.size() );
        _queueCount = static_cast<int32>( _crafter.getQueue().size() );
    }

    void CraftingDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( CraftingDemoComponentInternal::kStateFormat );
        if ( io.isWriting() )
        {
            io.value( _crafter );
            return;
        }
        Crafter crafter;
        crafter.initialize( _pRecipes.get() );
        io.value( crafter );
        if ( io.isOk() == false )
            return;
        _crafter = std::move( crafter );
    }
} // namespace sw
