/**
 * @file AnimationGraphLayerEdit.h
 * @brief 애니메이션 그래프의 레이어 · 본 마스크 목록을 고치는 명령입니다. ImGui 없이 그래프 값만 다룹니다.
 * @details 그리기는 `AnimationGraphLayerEditor` 가 맡고, 고른 항목 번호와 되돌리기 이름도 거기에 남습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    class AnimationGraphAsset;

    struct AnimationBoneMaskDef;
    struct AnimationGraphLayerDef;
} // namespace sw

namespace sw::editor
{
    /** @brief 프리셋 칸을 고른 결과입니다. 값이 그대로면 `Failed` 입니다. */
    enum class BoneMaskPresetResult : uint8
    {
        Embedded, ///< 프리셋을 비워 지금 항목을 그래프에 넣었다
        Loaded,   ///< 프리셋 파일로 항목을 채웠다
        Failed    ///< 읽지 못해 마스크가 그대로다(이유는 로그에 있다)
    };

    /**
     * @struct AnimationGraphLayerEdit
     * @brief 레이어 · 마스크 목록 명령입니다.
     */
    struct AnimationGraphLayerEdit
    {
        /** @brief 빈 이름과 첫 클립을 가진 레이어를 끝에 더하고 그 번호를 돌려줍니다. */
        static int32 addLayer( AnimationGraphAsset& inoutGraph );
        /** @brief @p index 의 레이어를 @p step(-1 · +1) 만큼 옮기고 새 번호를 돌려줍니다. 범위를 벗어나면 그대로 @p index 입니다. */
        static int32 moveLayer( AnimationGraphAsset& inoutGraph, int32 index, int32 step );
        /** @brief 새 마스크(`spine` 과 그 자손)를 끝에 더하고 그 번호를 돌려줍니다. */
        static int32 addMask( AnimationGraphAsset& inoutGraph );
        /** @brief 그 마스크를 쓰는 첫 레이어입니다. 없으면 nullptr 입니다. */
        static const AnimationGraphLayerDef* findLayerUsingMask( const AnimationGraphAsset& graph, const hashed_string& maskName );
        /** @brief 마스크를 지웁니다. 레이어가 쓰는 마스크는 저장한 그래프가 로드 오류가 되므로 지우지 않고 false 입니다. */
        [[nodiscard]] static bool removeMask( AnimationGraphAsset& inoutGraph, int32 index );
        /** @brief 프리셋 칸 값을 적용합니다. 빈 글이면 지금 항목을 그래프에 넣고, 아니면 파일을 읽어 항목을 바꿉니다. */
        [[nodiscard]] static BoneMaskPresetResult applyMaskPreset( AnimationBoneMaskDef& inoutMask, string_view presetPath );
    };
} // namespace sw::editor
