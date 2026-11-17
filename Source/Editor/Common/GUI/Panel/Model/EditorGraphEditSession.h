/**
 * @file EditorGraphEditSession.h
 * @brief 노드 그래프 문서 패널(대화 그래프 · 애니메이션 그래프)이 함께 쓰는 Model 쪽 편집 알림 입구입니다. ImGui 를 모릅니다.
 * @details 그래프 문서는 공용 그래프 틀(`EditorGraphDocumentPanel`)이 들고 패널마다 명령 묶음(`DialogueGraphEdit` · `AnimationGraphEdit` — Model)이 고친다.
 *          알림(되돌리기 한 단계 · 미저장)과 그 이름은 명령 묶음만 낸다 — 이 입구의 알림 함수는 private 이고 두 명령 묶음만 friend 다(감사 E-16).
 *          View 는 틀이 준 입구(`getGraphEditSession`)를 명령에 넘기기만 하고 스스로 알릴 수 없다. 언리얼 그래프 에디터의 스키마 액션이 트랜잭션을 여는 자리와 같다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    class DocumentEditRecorder;
    struct DialogueGraphEdit;
    struct AnimationGraphEdit;

    /**
     * @class EditorGraphEditSession
     * @brief 그래프 문서 패널 하나의 편집 알림 입구입니다. 파일 머리말 참고.
     */
    class SW_EDITOR_API EditorGraphEditSession final
    {
    public:
        /** @param recorder 문서 패널의 편집 알림 창구입니다(패널이 쥐어 이 입구보다 먼저 사라지지 않는다). */
        explicit EditorGraphEditSession( const DocumentEditRecorder& recorder )
            : _pRecorder{ &recorder }
        {
        }

    private:
        friend struct DialogueGraphEdit;  // 대화 그래프 명령이 고친 뒤 알린다
        friend struct AnimationGraphEdit; // 애니메이션 그래프 명령이 고친 뒤 알린다
        /** @brief 문서를 고쳤음을 @p label(명령이 정한 되돌리기 이름)로 알립니다. */
        void recordDocumentEdit( string_view label ) const;

    private:
        const DocumentEditRecorder* _pRecorder;
    };
} // namespace sw::editor
