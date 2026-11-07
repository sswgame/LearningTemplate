/**
 * @file EditorViewportPreview.h
 * @brief 도구 패널 미리보기를 선택 오브젝트·씬에 적용합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct FunctionInfo;
    struct SequenceBindingView;
    struct SequenceSpanState;
    struct TypeInfo;

    class Material;
    class SequenceAsset;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorViewportPreview
     * @brief Animation / Dialogue / Sequencer / Material 미리보기를 씬 뷰에 반영합니다.
     */
    class EditorViewportPreview
    {
    public:
        /**
         * @brief 대사 미리보기 메서드의 종류입니다 — `FUNCTION( EditorPreview = "DialogueLine" )`, 인자는 ( string graphPath, uint32 speakerID, string text ),
         *        돌려주는 값은 그 화자에 묶인 오브젝트의 `GameObjectHandle` 입니다(그 그래프를 돌리지 않는 컴포넌트는 무효 핸들).
         */
        static constexpr const utf8* kDialogueLinePreview = "DialogueLine";

        /** @brief 선택한 오브젝트와 이 그래프를 쓰는 오브젝트들의 스프라이트 애니메이터에서 노드 이름의 클립을 재생합니다. */
        static void applyAnimationNode( string_view nodeName, string_view graphPath = {} );
        /**
         * @brief 시퀀스 프레임을 활성 씬에 적용합니다 — 클립 · 컷 · 키 트랙(`applyFrame`)과 구간 동작(애니메이션 · 소리 · 자막, `applySpans`)입니다. 대상은 @p bindings 로 찾는다.
         * @param inoutListSpan 미리보기가 들어간 구간 상태(패널이 쥔다 — 멈출 때 `SequenceTimelineUtil::releaseSpans`).
         */
        static void applySequenceFrame( const sw::SequenceAsset& asset, const sw::SequenceBindingView& bindings, int32 frame, float32 framesPerSecond, vector<sw::SequenceSpanState>& inoutListSpan );
        /**
         * @brief 대사 미리보기 메서드(`EditorPreview = "DialogueLine"`)가 있는 컴포넌트마다 그래프 @p graphPath 의 대사를 넣고, 그 화자에 묶인 오브젝트를 선택합니다.
         * @details 컴포넌트는 타입 이름이 아니라 메서드 메타로 찾습니다 — EditorModule 은 GameFramework 를 링크하지 않으므로
         *          어느 모듈의 컴포넌트든 메타만 달면 미리보기를 받습니다. 화자 오브젝트는 그 컴포넌트의 화자 표가 돌려준 핸들이다 — 이름으로 찾지 않는다(묶인 곳이 없으면 고르지 않는다).
         */
        static void applyDialogueLine( string_view graphPath, uint32 speakerID, string_view text );
        /** @brief 선택 메시/스프라이트에 머티리얼을 붙이고 캐시를 갱신합니다. */
        static void applyMaterial( sw::Material* pMaterial, string_view assetPath );

        /** @brief @p type 과 기반 타입에서 `EditorPreview` 메타가 @p previewKind 인 메서드를 찾습니다. 없으면 nullptr 입니다. */
        static const FunctionInfo* findPreviewMethod( const TypeInfo& type, string_view previewKind );
    };
} // namespace sw::editor
