#include "pch.h"

#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

#include "Games/Playground/FeatureDemo/FeatureDemoCommandUtil.h"

namespace sw
{
    class CharacterEquipmentComponent;
    class Inventory;

    /**
     * @class CraftingDemoComponent
     * @brief 작업대 하나의 대기열을 프레임마다 흘립니다. 결과를 넣을 자리가 없으면 그 작업은 기다립니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Crafting Demo", Tooltip = "Crafting queue of one station over a character's bag" )
    class CraftingDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        CraftingDemoComponent();
        virtual ~CraftingDemoComponent() override;

        /** @brief 레시피를 @p count 번 대기열에 넣습니다. 재료는 지금 가방에서 나갑니다. 캐릭터가 없으면 비어 있습니다. */
        optional<CraftResult> enqueue( const hashed_string& recipeID, int32 count );
        /** @brief 대기열 @p index 를 빼고 재료를 돌려받습니다. 돌려줄 자리가 없거나 없는 자리면 false 입니다. */
        [[nodiscard]] bool cancel( int32 index );
        /** @brief 레시피를 배웁니다. */
        void learn( const hashed_string& recipeID ) { _crafter.learnRecipe( recipeID ); }
        /** @brief 제작 레벨을 정합니다. */
        void setLevel( int32 level ) { _level = level; }

        const hashed_string& getLastResult() const { return _lastResult; }

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( GameSeconds frameTime ) override;

    private:
        /** @brief 같은 씬 캐릭터의 가방입니다(장비 컴포넌트가 시작했을 때만). */
        [[nodiscard]] Inventory* findBag() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Recipes", Tooltip = "Recipe catalog (*.recipes.xml)" )
        AssetRef<RecipeCatalog> _recipePath;
        PROPERTY( Category = "Data", DisplayName = "Station", Tooltip = "Station id recipes ask for" )
        hashed_string _station;
        PROPERTY( Category = "Data", DisplayName = "Start Level", Min = 1 )
        int32 _startLevel;
        PROPERTY( Category = "Scene", DisplayName = "Character", Tooltip = "Object with the CharacterEquipmentComponent whose bag is used" )
        hashed_string _characterName;

        PROPERTY( RuntimeState, Min = 0 )
        int32 _level;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _finishedCount;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _queueCount;
        PROPERTY( RuntimeState )
        hashed_string _lastResult; ///< 마지막 명령의 `CraftResult` 이름

        BoundAssetRef<RecipeCatalog> _pRecipes; ///< 제작이 빌려 쓴다
        SW_CODEC_STATE Crafter       _crafter;
    };

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

#include "CraftingDemoComponent.cpp.gen.inl"
