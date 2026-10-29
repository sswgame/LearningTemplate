/**
 * @file DialogueGraphPreviewModel.h
 * @brief 대화 그래프 창의 미리보기 모델 — 지금 노드, 재생 · 한 단계 · 멈춤, 대사 노드에 머무는 시간으로 저절로 넘기기, 뷰포트에 대사 보이기. ImGui 를 모릅니다.
 * @details 패널(`DialogueGraphPanel`)은 문서 그래프 · 캔버스 · 인스펙터를 그리고 단추가 이것을 부르기만 한다 — 애니메이션 그래프 미리보기
 *          (`AnimationGraphPreviewModel`)와 같은 선이다. 그래프는 부를 때 넘긴다(모델은 문서를 갖지 않는다). 다음 노드는 러너와 같은 `DialogueCursor::step` 이 정한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class DialogueGraphAsset;
    struct DialogueStepInput;
} // namespace sw

namespace sw::editor
{
    /**
     * @class DialogueGraphPreviewModel
     * @brief 대화 그래프 창의 미리보기 상태와 명령입니다. 파일 머리말 참고.
     */
    class DialogueGraphPreviewModel
    {
    public:
        /** @brief 재생 중 대사 노드에 머무는 시간(초)입니다. 지나면 다음 노드로 넘긴다. */
        static constexpr float32 kHoldSeconds = 0.9f;

        /** @brief 다른 문서를 읽었다 — 재생을 멈추고 지금 노드를 잊습니다(옛 문서의 노드 id 로 새 문서를 걷지 않는다). */
        void resetForDocument() { stop(); }
        /** @brief @p graph 의 시작 노드부터 재생합니다. */
        void play( const DialogueGraphAsset& graph );
        /** @brief @p input 으로 한 단계 넘깁니다. 지금 노드가 없으면 시작 노드로 갑니다. */
        void step( const DialogueGraphAsset& graph, const DialogueStepInput& input );
        void stop();
        /** @brief 재생 중이면 기다리지 않는 노드를 지나고, 대사 노드는 `kHoldSeconds` 뒤 넘깁니다. 선택지 · 조건은 사람이 고른다. 그리기마다 부릅니다. */
        void update( const DialogueGraphAsset& graph, float32 deltaSeconds );

        /** @brief 미리보기의 지금 노드입니다. 0 이면 없다. */
        int32 getNodeID() const { return _nodeID; }

    private:
        /** @brief 그 노드로 옮깁니다. 대사나 선택지 노드이면 뷰포트에 보이고, 끝 노드나 없는 노드이면 멈춥니다. */
        void enterNode( const DialogueGraphAsset& graph, int32 nodeID );

    private:
        float32 _holdSeconds{ 0.0f }; ///< 지금 노드에 머문 시간
        int32   _nodeID{ 0 };
        bool    _bPlaying{ false };
    };
} // namespace sw::editor
