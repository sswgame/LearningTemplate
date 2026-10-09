/**
 * @file EditorViewportPreview.h
 * @brief 도구 패널 미리보기를 선택 오브젝트·씬에 적용합니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct FunctionInfo;
    struct TypeInfo;

    class Material;
    class SequenceAsset;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorViewportPreview
     * @brief Anim / Dialogue / Sequencer / Material 미리보기를 씬 뷰에 반영합니다.
     */
    class EditorViewportPreview
    {
    public:
        /** @brief 대사 미리보기 메서드의 종류입니다 — `FUNCTION( EditorPreview = "DialogueLine" )`, 인자는 ( string speaker, string text ). */
        static constexpr const utf8* kDialogueLinePreview = "DialogueLine";

        /** @brief 선택한 오브젝트와 이 그래프를 쓰는 오브젝트들의 스프라이트 애니메이터에서 노드 이름의 클립을 재생합니다. */
        static void applyAnimationNode( string_view nodeName, string_view graphPath = {} );
        /** @brief 시퀀스 프레임의 활성 클립을 대상 오브젝트 활성/트랜스폼에 적용합니다. */
        static void applySequenceFrame( const sw::SequenceAsset& asset, int32 frame );
        /**
         * @brief 화자 오브젝트를 선택하고, 대사 미리보기 메서드(`EditorPreview = "DialogueLine"`)가 있는 컴포넌트마다 대사를 넣습니다.
         * @details 컴포넌트는 타입 이름이 아니라 메서드 메타로 찾습니다 — EditorModule 은 GameFramework 를 링크하지 않으므로
         *          어느 모듈의 컴포넌트든 메타만 달면 미리보기를 받습니다.
         */
        static void applyDialogueLine( string_view speaker, string_view text );
        /** @brief 선택 메시/스프라이트에 머티리얼을 붙이고 캐시를 갱신합니다. */
        static void applyMaterial( sw::Material* pMaterial, string_view assetPath );

        /** @brief @p type 과 기반 타입에서 `EditorPreview` 메타가 @p previewKind 인 메서드를 찾습니다. 없으면(Shipping 포함) nullptr 입니다. */
        static const FunctionInfo* findPreviewMethod( const TypeInfo& type, string_view previewKind );
    };
} // namespace sw::editor
