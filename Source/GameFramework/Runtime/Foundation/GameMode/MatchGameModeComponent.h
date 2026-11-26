/**
 * @file MatchGameModeComponent.h
 * @brief 이기고 지는 한 판을 돌리고 미니게임을 스스로 끝내는 디렉터의 베이스입니다.
 * @details 언리얼 `AGameMode` 의 매치 상태(`EndMatch`)에 해당한다 — 끝이 없는 판(마을 · 도시 · 공원)의 디렉터는 `GameModeComponent` 에서 바로 나온다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /**
     * @class MatchGameModeComponent
     * @brief 판의 끝 흐름(결과 → 결과를 보이는 시간 → 미니게임 끝내기)을 쥔 디렉터입니다.
     * @details 파생은 걸음(`updateMatch`)과 끝을 알리는 한 줄(`endMatch`)만 적는다. 끝난 판에서는 걸음이 불리지 않고, 결과를 보이는 시간이 다 되면 베이스가 미니게임을 한 번 끝낸다.
     *          끝내기(`finishMinigame`)는 이 클래스 안에만 있다 — 파생이 유지 시간을 건너뛰거나 두 번 끝낼 수 없다.
     */
    REFLECT( Abstract, Category = "Framework", DisplayName = "Match Game Mode", Tooltip = "Base of the game modes whose match ends with a result and finishes the minigame" )
    class SW_GF_API MatchGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr const utf8* kWinResult  = "win";
        static constexpr const utf8* kLoseResult = "lose";
        static constexpr const utf8* kDrawResult = "draw";

        MatchGameModeComponent();
        virtual ~MatchGameModeComponent() override;

        /** @brief 판이 끝났으면(`endMatch`) true 입니다. 다시 연 판(`openGame`)은 false 입니다. */
        bool isMatchEnded() const { return _endResult.empty() == false; }
        /** @brief 끝난 판의 결과 이름입니다. 도는 판이면 빈 이름입니다. */
        const hashed_string& getEndResult() const { return _endResult; }

    protected:
        /** @brief 결과를 보이지 않고 바로 끝내는 디렉터면 @p resultHoldSeconds 를 0 으로 엽니다. */
        explicit MatchGameModeComponent( float32 resultHoldSeconds );

        /** @brief 도는 판의 걸음입니다. 시간이 흐른 프레임에만(@p deltaTime 0 초과), 판이 끝날 때까지 불립니다(`updateGame` 계약). */
        virtual void updateMatch( float32 deltaTime ) = 0;
        /**
         * @brief 디렉터 계약 ③ — 판을 결과 @p resultName(`kWinResult` 등)으로 끝냅니다. `_resultHoldSeconds` 뒤 미니게임을 끝내며, 0 이면 지금 끝낸다.
         * @details 판마다 한 번이다. 넘길 값은 미니게임을 끝낼 때 `collectMinigameResult` 가 모은다.
         */
        void endMatch( const hashed_string& resultName );

        /** @brief 도는 판이면 `updateMatch` 를, 끝난 판이면 결과를 보이는 시간을 흘립니다. */
        void updateGame( float32 deltaTime ) override;

    private:
        /**
         * @brief 이 디렉터가 도는 미니게임을 `_endResult` 로 끝냅니다. 켜진 미니게임이 없으면 오류를 남긴다(허브 판에서 부르면 계약 위반이다).
         * @details 나가기는 다음 게임 인스턴스 갱신(틱 밖)이 한다(`MinigameFlow::requestLeave`) — 하위 레벨 나가기가 이 디렉터를 지우므로 규칙 안에서 바로 나가지 않는다.
         */
        void finishMinigame();

    private:
        PROPERTY( Category = "Match", DisplayName = "Result Hold", Tooltip = "Seconds the result stays on screen before the minigame finishes", Min = 0.0, Units = s )
        float32 _resultHoldSeconds;

        PROPERTY( RuntimeState )
        hashed_string _endResult; ///< 끝난 판의 결과 이름(도는 판은 빈 이름)
        PROPERTY( RuntimeState )
        float32 _resultHoldLeft; ///< 끝난 뒤 미니게임을 끝내기까지 남은 시간(`Countdown` 의 남은 시간 — 다 된 걸음에 한 번 끝낸다)
    };
} // namespace sw
