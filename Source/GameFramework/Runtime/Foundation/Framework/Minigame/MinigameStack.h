/**
 * @file MinigameStack.h
 * @brief 미니게임 칸의 스택입니다 — 칸 하나는 켜진(또는 덮인) 미니게임과 그 미니게임을 덮기 전으로 돌아가는 길(돌아올 자리)입니다.
 * @details `MinigameFlow` 가 든다. 쌓기(`enter` — 미니게임 안에서 또 미니게임, 용과 같이의 가게 안 오락기)와 맨 위 교체(`switchTo` — 마리오 파티식 연속)를 같은 칸 모양으로 푼다.
 *          돌아올 자리는 들어간 방식마다 다른 값이라 한 형식(`std::variant`)으로 둔다 — 씬 전환은 찍은 씬 · 저장 막기, 하위 레벨은 얹은 씬 · 하위 레벨 이름, 첫 진입은 입구 씬.
 *          나중에 "세션 이동"(넷 세션의 서버 이동) 갈래를 더할 자리다. 덮인 미니게임의 정의는 그 아래 칸이 든다 — 돌아올 때 다시 켤 정의를 따로 적지 않는다.
 *          엔진 서비스를 부르지 않는 값 형식이라 단위 테스트가 그대로 쓴다(`MinigameTest`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"

#include <variant>

namespace sw
{
    /** @brief 씬 전환으로 들어간 칸의 돌아올 자리입니다 — 들어갈 때의 활성 씬과 그 스냅숏, 칸이 있는 동안 쥐는 저장 막기. */
    struct MinigameSuspendedScene
    {
        string        _scenePath; ///< 다시 열 씬(리소스 id)
        vector<uint8> _snapshot;  ///< 찍은 씬(`SerializePurpose::Snapshot`)
        Subscription  _saveBlock; ///< 칸이 있는 동안 저장을 막는다(`MinigameFlow::kSaveBlockReason`) — 칸이 사라지면 풀린다
    };

    /** @brief 하위 레벨로 들어간 칸의 돌아올 자리입니다 — 얹은 씬과 하위 레벨 이름(나올 때 뺀다). */
    struct MinigameSubLevelReturn
    {
        string        _scenePath;    ///< 하위 레벨을 얹은 바탕 씬(리소스 id) — 아직 활성일 때만 뺀다
        hashed_string _subLevelName; ///< 하위 레벨 이름(미니게임 이름)
    };

    /** @brief 돌아올 씬 없이 들어간 칸입니다(첫 진입이 미니게임 — `gv_firstMinigame`) — 나가면 gamesettings 입구 씬으로 간다. */
    struct MinigameEntranceReturn
    {
    };

    /** @brief 돌아올 자리입니다. 갈래를 더하면 나가기(`MinigameFlow::leave`)의 `std::visit` 이 빠진 갈래를 컴파일 오류로 알린다. */
    using MinigameReturnPoint = std::variant<MinigameSuspendedScene, MinigameSubLevelReturn, MinigameEntranceReturn>;

    /** @brief 스택 칸 하나입니다. */
    struct MinigameFrame
    {
        MinigameDefinition  _definition;  ///< 이 칸의 미니게임
        MinigameReturnPoint _returnPoint; ///< 이 미니게임을 덮기 전으로 돌아가는 길
    };

    /** @brief 결과에 실려 바탕 판으로 넘어가는 값 하나입니다(번 돈 · 얻은 물건 수 · 점수). 이름의 뜻은 결과를 받는 쪽(허브 디렉터)이 정한다. */
    struct MinigameResultValue
    {
        hashed_string _name; ///< 값 이름(예: 화폐 id · 아이템 id · `score`)
        int64         _amount{ 0 };
    };

    /**
     * @brief 미니게임이 끝나며 넘기는 결과입니다 — 돌아올 자리를 복원한 뒤에 넘긴다(복원이 파생 인스턴스 상태까지 되감으므로 번 것은 그 뒤에 얹는다).
     * @details 한 판 임시 상태(미니게임 지갑 · 창고 · 점수)는 디렉터가 소유하고 바탕 판(플레이어 진행 · 월드 상태)에 직접 쓰지 않는다 — 이 값 목록으로만 내보낸다
     *          (언리얼 매치 상태가 PlayerState 를 거치지 않고 끝에 정산하는 자리, `GameModeComponent::finishMinigame`).
     */
    struct MinigameResult
    {
        hashed_string               _minigame;   ///< 끝난 미니게임 이름
        hashed_string               _resultName; ///< 결과 이름(미니게임이 정한다 — 예: `win` · `lose`). 비었으면 결과 없이 나왔다
        vector<MinigameResultValue> _listValue;  ///< 바탕 판에 넘길 값(디렉터의 `collectMinigameResult`). 비었으면 넘길 것이 없다
    };

    /**
     * @class MinigameStack
     * @brief 미니게임 칸의 스택입니다. 맨 위 칸만 켜져 있고, 아래 칸들은 덮여 꺼져 있다(`MinigameFlow`).
     */
    class SW_GF_API MinigameStack
    {
    public:
        // 칸은 돌아올 씬의 소유를 들고 있어 복사할 수 없다 — 내보낸 클래스가 복사를 만들지 않게 막는다.
        MinigameStack()                                  = default;
        MinigameStack( const MinigameStack& )            = delete;
        MinigameStack& operator=( const MinigameStack& ) = delete;
        MinigameStack( MinigameStack&& )                 = default;
        MinigameStack& operator=( MinigameStack&& )      = default;

        /** @brief 칸이 없으면 true 입니다. */
        bool isEmpty() const { return _listFrame.empty(); }
        /** @brief 칸 수입니다(쌓인 깊이). */
        uint32 getDepth() const { return static_cast<uint32>( _listFrame.size() ); }
        /** @brief 칸을 하나 더 쌓아도 깊이 @p depthLimit 를 넘지 않으면 true 입니다. 한도가 0 이면(설정이 없다) false 입니다. */
        bool canPush( uint32 depthLimit ) const { return depthLimit > 0 && getDepth() < depthLimit; }

        /** @brief 칸을 맨 위에 쌓습니다. 한도는 부르는 쪽이 `canPush` 로 먼저 본다. */
        void push( MinigameFrame frame );
        /** @brief 맨 위 칸을 꺼냅니다. 비었으면 false 이고 @p outFrame 은 그대로입니다. */
        [[nodiscard]] bool pop( MinigameFrame& outFrame );
        /** @brief 칸을 모두 버립니다(저장 막기도 풀린다). */
        void clear() { _listFrame.clear(); }

        /** @brief 맨 위 칸입니다. 비었으면 nullptr 입니다. */
        MinigameFrame*       findTop() { return _listFrame.empty() ? nullptr : &_listFrame.back(); }
        const MinigameFrame* findTop() const { return _listFrame.empty() ? nullptr : &_listFrame.back(); }
        /** @brief 맨 위 칸 바로 아래 칸(맨 위가 덮은 미니게임)입니다. 없으면 nullptr 입니다. */
        const MinigameFrame* findCovered() const { return _listFrame.size() < 2 ? nullptr : &_listFrame[_listFrame.size() - 2]; }
        /** @brief 씬 전환 칸(저장 막기를 쥔 칸)이 하나라도 있으면 true 입니다. */
        bool hasSuspendedScene() const;

    private:
        vector<MinigameFrame> _listFrame; ///< 아래부터 위로
    };
} // namespace sw
