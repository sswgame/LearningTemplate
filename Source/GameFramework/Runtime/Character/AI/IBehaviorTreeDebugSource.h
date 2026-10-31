/**
 * @file IBehaviorTreeDebugSource.h
 * @brief 컴포넌트에 안 붙은 행동 트리 실행기(예: RTS 지휘관 AI)를 행동 트리 창에 보이는 작은 인터페이스입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class BehaviorTreeRunner;

    /** @brief 실행기 하나와 그 이름, 실행기가 쥔 트리의 파일 경로입니다. */
    struct BehaviorTreeDebugRunner
    {
        string                    _name{};     ///< 창에 보일 이름(예: "Player 0")
        string                    _treePath{}; ///< 실행기가 쥔 트리를 읽은 `*.bt.xml` 의 리소스 id
        const BehaviorTreeRunner* _pRunner{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IBehaviorTreeDebugSource
     * @brief 행동 트리 실행기 목록을 내놓는 쪽의 계약입니다. 디렉터(`GameDirectorComponent::findBehaviorTreeDebugSource`)가 자기 자신을 돌려줍니다.
     * @details 실행기의 노드 번호는 깊이 우선 번호라 창에 연 파일과 실행기가 쥔 트리가 같은 파일일 때만 맞다 — 그래서 경로를 함께 내놓고 창이 다르면 표시하지 않는다.
     */
    class IBehaviorTreeDebugSource
    {
    public:
        virtual ~IBehaviorTreeDebugSource() = default;

        /** @brief 지금 도는 실행기를 @p outList 에 더합니다(비우지 않는다). 실행기 포인터는 다음 틱까지만 유효하다. */
        virtual void collectBehaviorTreeRunners( vector<BehaviorTreeDebugRunner>& outList ) const = 0;
    };
} // namespace sw
