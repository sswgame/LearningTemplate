#include "pch.h"

#include "Games/Playground/FeatureDemo/TurnOrderDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "Games/Playground/FeatureDemo/FeatureDemoCommandUtil.h"

namespace sw
{
    namespace
    {
        struct TurnOrderDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "FDTO" );
            static constexpr uint32 kStateVersion = 1;

            /** @brief 명령 인자 `<demo object> <actor> …` 의 앞 둘을 읽습니다. 시연이 없거나 없는 행위자면 까닭을 적고 nullptr 입니다. */
            [[nodiscard]] static TurnOrderDemoComponent* findActor( const vector<string>& listArgument, int32& outActorID, string& outReply )
            {
                if ( StringUtil::parseInt( listArgument[1], outActorID ) == false )
                    return nullptr;
                TurnOrderDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<TurnOrderDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr || pDemo->hasActor( outActorID ) )
                    return pDemo;
                FeatureDemoCommandUtil::reply( outReply, "no actor %# in the turn order", outActorID );
                return nullptr;
            }

            static void writeReply( const TurnOrderDemoComponent& demo, string& outReply )
            {
                FeatureDemoCommandUtil::reply( outReply, "turn %#, next %#", demo.getCurrentActor(), demo.getOrderText() );
            }

            static bool runNext( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                TurnOrderDemoComponent* pDemo = FeatureDemoCommandUtil::findStarted<TurnOrderDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->advanceTurn();
                writeReply( *pDemo, outReply );
                return true;
            }

            static bool runDelay( const vector<string>& listArgument, string& outReply )
            {
                float32 amount  = 0.0f;
                int32   actorID = 0;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], amount ) == false )
                    return false;
                TurnOrderDemoComponent* pDemo = findActor( listArgument, actorID, outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->delayActor( actorID, amount );
                writeReply( *pDemo, outReply );
                return true;
            }

            static bool runPriority( const vector<string>& listArgument, string& outReply )
            {
                int32 priority = 0;
                int32 actorID  = 0;
                if ( listArgument.size() != 3 || StringUtil::parseInt( listArgument[2], priority ) == false )
                    return false;
                TurnOrderDemoComponent* pDemo = findActor( listArgument, actorID, outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->setPriority( actorID, priority );
                writeReply( *pDemo, outReply );
                return true;
            }

            static bool runRemove( const vector<string>& listArgument, string& outReply )
            {
                int32 actorID = 0;
                if ( listArgument.size() != 2 )
                    return false;
                TurnOrderDemoComponent* pDemo = findActor( listArgument, actorID, outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->removeActor( actorID );
                writeReply( *pDemo, outReply );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( TurnNext, "turn.next", "turn.next <demo object>", "Give the turn to the next actor of a turn order demo", &TurnOrderDemoComponentInternal::runNext );
    SW_DEV_COMMAND( TurnDelay, "turn.delay", "turn.delay <demo object> <actor> <turns>", "Delay an actor of a timeline turn order demo", &TurnOrderDemoComponentInternal::runDelay );
    SW_DEV_COMMAND( TurnPriority, "turn.priority", "turn.priority <demo object> <actor> <priority>", "Set an actor's priority and restart the round of a turn order demo", &TurnOrderDemoComponentInternal::runPriority );
    SW_DEV_COMMAND( TurnRemove, "turn.remove", "turn.remove <demo object> <actor>", "Take an actor out of a turn order demo", &TurnOrderDemoComponentInternal::runRemove );

    TurnOrderDemoComponent::TurnOrderDemoComponent()
        : _mode{ TurnOrderMode::Rounds }
        , _listSpeed{}
        , _previewCount{ 6 }
        , _seed{ 1 }
        , _currentActor{ constant::invalid_index::kInt32 }
        , _round{ 0 }
        , _orderText{}
        , _order{}
    {
    }

    TurnOrderDemoComponent::~TurnOrderDemoComponent() = default;

    bool TurnOrderDemoComponent::startGame()
    {
        if ( _listSpeed.empty() )
        {
            SW_LOG_ERROR_ID( "FeatureDemo.TurnOrderNoActors", "Turn order demo needs at least one actor speed" );
            return false;
        }
        _order.initialize( _mode, _seed );
        for ( size_t actorIndex = 0; actorIndex < _listSpeed.size(); ++actorIndex )
        {
            _order.addActor( static_cast<int32>( actorIndex ), _listSpeed[actorIndex] );
        }
        refreshState();
        return true;
    }

    void TurnOrderDemoComponent::refreshState()
    {
        vector<int32> listActor;
        _order.previewOrder( _previewCount, listActor );
        StringBuilder<constant::buffer::kMax128> text;
        for ( const int32 actorID : listActor )
        {
            text.appendFormat( "%#%#", text.size() == 0 ? "" : ",", actorID );
        }
        _orderText = string( text.view() );
        _round     = _order.getRound();
    }

    void TurnOrderDemoComponent::advanceTurn()
    {
        _currentActor = _order.next();
        refreshState();
    }

    void TurnOrderDemoComponent::delayActor( int32 actorID, float32 amount )
    {
        _order.delayActor( actorID, amount );
        refreshState();
    }

    void TurnOrderDemoComponent::setPriority( int32 actorID, int32 priority )
    {
        _order.setPriority( actorID, priority );
        _order.restartRound();
        refreshState();
    }

    void TurnOrderDemoComponent::removeActor( int32 actorID )
    {
        _order.removeActor( actorID );
        refreshState();
    }

    void TurnOrderDemoComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void TurnOrderDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)manager;
        (void)bRespawnViews;
    }

    void TurnOrderDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( TurnOrderDemoComponentInternal::kStateTag, TurnOrderDemoComponentInternal::kStateVersion );
        if ( io.isWriting() )
        {
            io.value( _order );
            return;
        }
        TurnOrder order;
        io.value( order );
        if ( io.isOk() == false )
            return;
        _order = std::move( order );
    }
} // namespace sw
