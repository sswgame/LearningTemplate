/**
 * @file WorldSettings.h
 * @brief 씬 하나의 월드 설정입니다(언리얼 `AWorldSettings` · World Partition 의 런타임 격자 설정 자리). 지금은 월드 스트리밍 여부와 그 격자 · 예산을 담습니다.
 *
 * [씬 파일]
 * 씬 루트의 `<WorldSettings>` 자식으로 리플렉션이 적고 읽습니다(`SceneDocument`). 씬 형식 판 2부터 **필수**입니다 — 없는 씬 파일은 읽지 않습니다.
 * 기본값으로 메우지 않는다: 스트리밍하지 않는 씬도 `_bStreaming="false"` 를 적는다.
 *
 * [스트리밍하는 씬]
 * 쿠커가 오브젝트 위치와 격자로 엔티티를 셀로 나누고(`SceneCooker`), 런타임은 스트리밍 원점 둘레의 셀을 올리고 내린다(`WorldStreamingSubsystem`).
 * 격자 · 내리는 여유 반경 · 프레임당 올리는 셀 수는 모두 이 데이터에서 온다. 코드에는 기본값이 없다 — 맞지 않으면 `validate` 가 오류로 멈춘다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/WorldCellGrid.h"

namespace sw
{
    /**
     * @struct WorldSettings
     * @brief 파일 머리말 참고.
     */
    REFLECT()
    struct SW_API WorldSettings
    {
        REFLECT_BODY();

        /** @brief 씬 파일에서 이 설정을 담는 요소 이름입니다(리플렉션 타입 이름과 같다). */
        static constexpr const utf8* kElementName = "WorldSettings";

        PROPERTY( Tooltip = "Load this scene in cells around streaming sources instead of all at once" )
        bool _bStreaming{ false };
        PROPERTY( Tooltip = "Cell grid the cooker splits the scene with; used only when streaming" )
        WorldCellGridDef _cellGrid{};
        PROPERTY( Min = 0.0, Tooltip = "A loaded cell is unloaded only when every source is farther than loading radius plus this", Units = m )
        float32 _unloadMargin{ 0.0f };
        PROPERTY( Min = 0, Tooltip = "Cells loaded per frame at most; a streaming scene needs at least 1" )
        uint32 _maxCellLoadsPerFrame{ 0 };

        /**
         * @brief 설정이 쓸 수 있는지 봅니다. 스트리밍하지 않으면 늘 true 입니다.
         * @details 스트리밍하면 격자를 만들 수 있어야 하고(`WorldCellGrid::tryCreate`), 프레임당 올리는 셀이 1 이상, 여유 반경이 0 이상이어야 합니다.
         *          어긋나면 씬 이름 @p sceneName 과 함께 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool validate( string_view sceneName ) const;
    };
} // namespace sw
