#include "pch.h"

#include "Games/Playground/FeatureDemo/CraftingDemoComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    namespace
    {
        struct CraftingDemoComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "FDCR" };
        };
    } // namespace

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
        _crafter.initialize( *_pRecipes );
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

    void CraftingDemoComponent::updateGame( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        Inventory*    pBag      = findBag();
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
        (void)io.valueOpened( _crafter, [this]( Crafter& crafter )
        {
            if ( _pRecipes == nullptr )
                return false; // 조합표 없이 연 상태는 읽지 않는다
            crafter.initialize( *_pRecipes );
            return true;
        } ); // 실패는 아카이브에 남는다
    }
} // namespace sw
