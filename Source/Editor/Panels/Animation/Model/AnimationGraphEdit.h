/**
 * @file AnimationGraphEdit.h
 * @brief 애니메이션 그래프 창의 문서 읽기 · 쓰기입니다. ImGui 를 모릅니다 — 창(View)이 커맨드 헤더를 보지 않게 여기를 지난다(`docs/agents/EditorUI.md` MVVM 계약, `DialogueGraphEdit` 와 같은 자리).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw
{
    class AnimationGraphAsset;
} // namespace sw

namespace sw::editor
{
    /** @brief 애니메이션 그래프 문서의 읽기 · 쓰기입니다. */
    struct AnimationGraphEdit
    {
        /** @brief Resource 상대 경로 @p path 의 애니메이션 그래프 문서를 읽습니다(`EditorToolAssetCommands::loadAnimationGraph`). */
        static ToolAssetLoadResult loadGraph( AnimationGraphAsset& outGraph, string_view path );
        /** @brief @p graph 를 @p path 에 씁니다. 쓰지 못하면 false 입니다(창은 저장 표시를 지우지 않는다). */
        [[nodiscard]] static bool saveGraph( const AnimationGraphAsset& graph, string_view path );
    };
} // namespace sw::editor
