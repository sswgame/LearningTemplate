/**
 * @file AnimationGraphPreviewModel.h
 * @brief 애니메이션 그래프 창의 미리보기 모델 — 플레이어가 빌려 쓰는 그래프 사본 · 플레이어, 재생 · 한 단계 · 멈춤, 상태에 머무는 시간으로 저절로 넘기기,
 *        넘어간 전이(캔버스 흐름 표시), 선택 오브젝트에 지금 상태 보이기. ImGui 를 모릅니다.
 * @details 패널(`AnimationGraphPanel`)은 문서 그래프(`EditorGraphDocumentPanel::_graph`) · 캔버스 · 인스펙터를 그리고 단추가 이것을 부르기만 한다 — 클립 미리보기
 *          (`AnimationClipPreviewModel`)와 같은 선이다(언리얼 AnimBP 에디터의 미리보기 인스턴스와 그래프 패널이 나뉜 것과 같다). 그래프는 부를 때 넘긴다(모델은 문서를 갖지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"

namespace sw::editor
{
    /**
     * @class AnimationGraphPreviewModel
     * @brief 애니메이션 그래프 창의 미리보기 상태와 명령입니다. 파일 머리말 참고.
     * @details 플레이어가 사본(`_previewGraph`)의 주소를 들므로 옮기거나 복사하지 않는다.
     */
    class AnimationGraphPreviewModel
    {
    public:
        /** @brief 재생 중 한 상태에 머무는 시간(초)입니다. 클립이 없어 "끝나면 다음" 을 이 시간마다 직접 진행한다. */
        static constexpr float32 kHoldSeconds = 0.75f;

        AnimationGraphPreviewModel();
        AnimationGraphPreviewModel( const AnimationGraphPreviewModel& )            = delete;
        AnimationGraphPreviewModel& operator=( const AnimationGraphPreviewModel& ) = delete;

        /** @brief 다른 문서를 읽었다 — 재생을 멈추고 지난 상태 · 흐름 표시를 잊습니다(옛 문서의 상태 이름으로 새 문서의 전이를 찾지 않는다). */
        void resetForDocument();
        /** @brief @p graph 의 사본으로 처음 상태부터 재생합니다. 선택 오브젝트(그래프 @p graphPath)에 지금 상태를 보입니다. */
        void play( const AnimationGraphAsset& graph, string_view graphPath );
        /** @brief @p graph 의 사본으로 한 단계 넘깁니다. 더 넘길 곳이 없으면 재생을 끕니다. */
        void advance( const AnimationGraphAsset& graph, string_view graphPath );
        void stop();
        /** @brief 재생 중이면 @p deltaSeconds 만큼 머물고, `kHoldSeconds` 가 지나면 지금 사본에서 한 단계 넘깁니다. 그리기마다 부릅니다. */
        void update( float32 deltaSeconds, string_view graphPath );

        const hashed_string& getCurrentStateName() const { return _previewPlayer.getCurrentStateName(); }
        /** @brief @p nodeName 이 미리보기의 지금 상태면 true 입니다. 상태가 없으면 false 입니다. */
        bool isCurrentState( string_view nodeName ) const;
        /** @brief 지난 단계에서 넘어간 전이 링크 id 를 꺼냅니다(한 번만 돌려준다). 없으면 0 입니다. 캔버스가 흐름을 보인다. */
        [[nodiscard]] int32 takeFlowLinkID();

    private:
        /** @brief 플레이어에 @p graph 의 사본을 넣습니다. */
        void syncGraph( const AnimationGraphAsset& graph );
        /** @brief 한 단계 넘기고(못 넘기면 재생을 끈다) 지금 상태를 보입니다. */
        void step( string_view graphPath );
        /** @brief 상태가 바뀌었으면 사본에서 그 전이 링크를 찾아 흐름 표시로 남깁니다. */
        void noteTransition();

    private:
        AnimationGraphAsset  _previewGraph;     ///< 미리보기 플레이어가 빌려 쓰는 그래프 복사본이다
        AnimationGraphPlayer _previewPlayer;    ///< 노드 이름만 넘긴다. 클립 없이 "끝나면 다음" 을 직접 진행한다
        string               _previewStateName; ///< 지난번 미리보기 상태다. 전이 흐름을 표시하는 데 쓴다
        float32              _holdSeconds;      ///< 지금 상태에 머문 시간
        int32                _flowLinkID;       ///< 다음 캔버스에서 흐름을 보여 줄 전이다. 0 이면 없다
        bool                 _bPlaying;
    };
} // namespace sw::editor
