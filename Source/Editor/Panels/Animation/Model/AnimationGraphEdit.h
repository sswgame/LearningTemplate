/**
 * @file AnimationGraphEdit.h
 * @brief 애니메이션 그래프 문서를 읽고 쓰는 명령입니다. ImGui 없이 그래프 값만 다룹니다.
 * @details 창(`AnimationGraphPanel`)이 커맨드 헤더(`EditorToolAssetCommands`)를 보지 않게 여기를 지난다 — 대화 그래프의 `DialogueGraphEdit` 와 같은 자리다(MVVM 계약).
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
    /**
     * @struct AnimationGraphEdit
     * @brief 애니메이션 그래프 문서 명령 묶음입니다.
     */
    struct AnimationGraphEdit
    {
        /** @brief Resource 상대 경로 @p path 의 애니메이션 그래프 문서를 읽습니다(`EditorToolAssetCommands::loadAnimationGraph`). */
        static ToolAssetLoadResult loadGraph( AnimationGraphAsset& outGraph, string_view path );
        /** @brief 애니메이션 그래프 문서를 @p path 에 씁니다. 실패하면 커맨드가 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool saveGraph( const AnimationGraphAsset& graph, string_view path );
    };
} // namespace sw::editor
