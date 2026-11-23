#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureComponents.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureReceiverComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct GameFeatureComponentsInternal
        {
            [[nodiscard]] static bool readAddedComponents( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GameFeatureComponents* const pComponents = game::findSubsystem<GameFeatureComponents>();
                if ( pComponents == nullptr )
                    return false;
                outValue = static_cast<float64>( pComponents->countAddedComponents() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( gameFeatureAddedComponents, "GameFeature.AddedComponents", "Components the active game features added to their receivers", &GameFeatureComponentsInternal::readAddedComponents );

    GameFeatureComponents::GameFeatureComponents()
        : _listRule{}
        , _listReceiver{}
        , _nextRuleID{ 1 }
    {
    }

    GameFeatureComponents::~GameFeatureComponents()
    {
        for ( GameFeatureReceiverComponent* pReceiver : _listReceiver )
        {
            pReceiver->bindComponents( nullptr );
        }
    }

    uint32 GameFeatureComponents::addRule( const hashed_string& receiver, const hashed_string& componentType )
    {
        GameFeatureComponentRule rule;
        rule._ruleID        = _nextRuleID++;
        rule._receiver      = receiver;
        rule._componentType = componentType;
        _listRule.push_back( rule );
        for ( GameFeatureReceiverComponent* pReceiver : _listReceiver )
        {
            if ( pReceiver->getReceiver() == receiver )
                pReceiver->addFeatureComponent( rule );
        }
        return rule._ruleID;
    }

    void GameFeatureComponents::removeRule( uint32 ruleID )
    {
        (void)VectorUtil::removeIf( _listRule, [ruleID]( const GameFeatureComponentRule& rule ) // 없는 번호면 붙인 것도 없다
        { return rule._ruleID == ruleID; } );
        for ( GameFeatureReceiverComponent* pReceiver : _listReceiver )
        {
            pReceiver->removeFeatureComponent( ruleID );
        }
    }

    void GameFeatureComponents::addReceiver( GameFeatureReceiverComponent& receiver )
    {
        if ( VectorUtil::contains( _listReceiver, &receiver ) )
            return;
        _listReceiver.push_back( &receiver );
        receiver.bindComponents( this );
        for ( const GameFeatureComponentRule& rule : _listRule )
        {
            if ( receiver.getReceiver() == rule._receiver )
                receiver.addFeatureComponent( rule );
        }
    }

    void GameFeatureComponents::removeReceiver( const GameFeatureReceiverComponent& receiver )
    {
        (void)VectorUtil::removeIf( _listReceiver, [&receiver]( const GameFeatureReceiverComponent* pOther ) // 두 번 빼도 된다
        { return pOther == &receiver; } );
    }

    uint32 GameFeatureComponents::countAddedComponents() const
    {
        uint32 count = 0;
        for ( const GameFeatureReceiverComponent* pReceiver : _listReceiver )
        {
            count += pReceiver->getAddedCount();
        }
        return count;
    }
} // namespace sw
