/**
 * @file AnimationGraphEdit.h
 * @brief 애니메이션 그래프 문서를 읽고 쓰는 명령입니다. ImGui 없이 그래프 값만 다룹니다.
 * @details 창(`AnimationGraphPanel`)이 커맨드 헤더(`EditorToolAssetCommands`)를 보지 않게 여기를 지난다 — 대화 그래프의 `DialogueGraphEdit` 와 같은 자리다(MVVM 계약).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw
{
    class AnimationGraphAsset;
    struct AnimationGraphPoseNodeDef;
    struct AnimationPoseNodeType;
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

        // 포즈 노드 종류 표(`AnimationPoseNodeRegistry` — 엔진 서비스)를 읽는 창구다. 패널 뷰는 서비스를 찾지 않는다(EditorLayers.toml).
        /** @brief 등록된 포즈 노드 종류 이름들입니다. 게임 · 플러그인이 더할 수 있어 메뉴를 열 때마다 다시 읽는다. */
        [[nodiscard]] static vector<hashed_string> getPoseNodeTypeNames();
        /** @brief 종류 @p typeName 의 기본 정의를 @p outDef 에 채웁니다. 모르는 종류면 표가 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool makeDefaultPoseNode( const hashed_string& typeName, AnimationGraphPoseNodeDef& outDef );
        /** @brief 종류 @p typeName 의 계약입니다. 모르는 종류면 nullptr 입니다. */
        [[nodiscard]] static const AnimationPoseNodeType* findPoseNodeType( const hashed_string& typeName );
    };
} // namespace sw::editor
