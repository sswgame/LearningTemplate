#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/Combat/TurnOrder.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

#include "Games/Playground/FeatureDemo/FeatureDemoCommandUtil.h"

namespace sw
{
    /**
     * @class TurnOrderDemoComponent
     * @brief 행위자 번호는 속도 목록의 자리다. 차례는 개발 명령 `turn.next` · `turn.delay` · `turn.priority` · `turn.remove` 로만 바뀝니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Turn Order Demo", Tooltip = "Turn order of actors by speed (rounds or timeline), driven by dev commands" )
    class TurnOrderDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        TurnOrderDemoComponent();
        virtual ~TurnOrderDemoComponent() override;

        /** @brief 다음 차례로 넘깁니다. */
        void advanceTurn();
        /** @brief 행위자의 차례를 @p amount(한 차례의 비율)만큼 늦춥니다(타임라인제). */
        void delayActor( int32 actorID, float32 amount );
        /** @brief 이번 라운드의 우선도를 정하고 순서를 다시 정합니다(라운드제). */
        void setPriority( int32 actorID, int32 priority );
        /** @brief 쓰러진 행위자를 뺍니다. */
        void removeActor( int32 actorID );
        /** @brief 이 번호의 행위자가 있으면 true 입니다. */
        bool hasActor( int32 actorID ) const { return _order.hasActor( actorID ); }

        int32         getCurrentActor() const { return _currentActor; }
        const string& getOrderText() const { return _orderText; }

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( GameSeconds frameTime ) override;

    private:
        /** @brief 상태 칸(라운드 · 앞으로의 차례)을 순서에서 옮깁니다. */
        void refreshState();

    private:
        PROPERTY( Category = "Turn Order", DisplayName = "Mode" )
        TurnOrderMode _mode;
        PROPERTY( Category = "Turn Order", DisplayName = "Speeds", Tooltip = "Speed of each actor (the index is the actor id)" )
        vector<float32> _listSpeed;
        PROPERTY( Category = "Turn Order", DisplayName = "Preview Count", Min = 1 )
        int32 _previewCount;
        PROPERTY( Category = "Turn Order", DisplayName = "Seed", Min = 1 )
        uint32 _seed;

        PROPERTY( RuntimeState, Min = -1 )
        int32 _currentActor; ///< 지금 차례(-1 은 아직 없음)
        PROPERTY( RuntimeState, Min = 0 )
        int32 _round;
        PROPERTY( RuntimeState )
        string _orderText; ///< 앞으로의 차례

        SW_CODEC_STATE TurnOrder _order;
    };

    namespace
    {
        struct TurnOrderDemoComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "FDTO" };

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

#include "TurnOrderDemoComponent.cpp.gen.inl"
