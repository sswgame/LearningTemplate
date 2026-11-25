/**
 * @file TurnOrderDemoComponent.h
 * @brief 턴 순서(`TurnOrder`)의 시연입니다 — 행위자 속도 목록으로 라운드제나 타임라인제 차례를 세우고, 개발 명령이 차례를 넘기고 늦추고 뺍니다.
 * @details 앞으로의 차례는 상태 칸 `_orderText`(행위자 번호를 쉼표로 이은 글)로 보인다. 시나리오: `game/playground/automation/turnorder.scenario.xml`(무대 maps/turnorder).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Combat/TurnOrder.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

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
        void               updateGame( float32 deltaTime ) override;

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
} // namespace sw
