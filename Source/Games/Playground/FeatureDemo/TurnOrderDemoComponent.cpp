#include "pch.h"

#include "Games/Playground/FeatureDemo/TurnOrderDemoComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct TurnOrderDemoComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "FDTO" };
        };
    } // namespace

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
        _currentActor = _order.next().value_or( constant::invalid_index::kInt32 );
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

    void TurnOrderDemoComponent::updateGame( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        (void)deltaTime;
    }

    void TurnOrderDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( TurnOrderDemoComponentInternal::kStateFormat );
        io.value( _order );
    }
} // namespace sw
