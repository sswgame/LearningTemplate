/**
 * @file DialogueCursor.h
 * @brief 대화 그래프에서 다음 노드를 정하는 규칙입니다. 런타임 러너와 에디터 미리보기가 같은 함수를 부릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw
{
    /** @brief 노드 하나를 지날 때 필요한 입력입니다. 노드의 `DialogueNodeFlow` 가 어느 칸을 읽는지 정합니다. */
    struct DialogueStepInput
    {
        int32 _choiceIndex{ -1 };      /**< `WaitChoice` 노드에서 고른 선택지입니다. 음수면 기본 출력 핀으로 갑니다. */
        bool  _bConditionMet{ false }; /**< `Condition` 노드의 조건식 결과입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @class DialogueCursor
     * @brief 대화 진행 규칙입니다. 다음 노드는 **이 함수 하나가** 정합니다.
     * @details 규칙은 노드의 `DialogueNodeOutput`(특성 표)에서 옵니다. 러너는 노드마다 할 일(대사 알림 · 조건 평가 · 명령 실행)만 하고
     *          다음 노드는 여기서 받습니다. 에디터 미리보기도 같은 함수를 불러, 미리보기와 게임이 다른 길을 갈 수 없습니다.
     */
    class SW_API DialogueCursor
    {
    public:
        /**
         * @brief `node` 를 지난 뒤의 다음 노드 id 입니다. 다음이 없으면 0(대화 끝)입니다.
         * @details 이어지지 않은 선택지 · 분기 핀은 기본 출력 핀으로 갑니다. 특성 표에 없는 타입은 0 입니다.
         */
        static int32 step( const DialogueGraphAsset& asset, const DialogueAssetNode& node, const DialogueStepInput& input );
        /** @brief 특성 표의 기본값으로 채운 새 노드를 만듭니다(화자 · 본문 · 선택지). */
        static DialogueAssetNode makeNode( DialogueAssetNodeType type, int32 nodeId );
    };
} // namespace sw
