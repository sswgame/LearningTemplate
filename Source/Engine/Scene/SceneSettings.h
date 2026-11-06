/**
 * @file SceneSettings.h
 * @brief 씬 하나의 월드 설정입니다(언리얼 `ASceneSettings` · World Partition 의 런타임 격자 설정 자리). 지금은 월드 스트리밍 여부와 그 격자 · 예산을 담습니다.
 *
 * [씬 파일]
 * 씬 루트의 `<SceneSettings>` 자식으로 리플렉션이 적고 읽습니다(`SceneDocument`). 씬 형식 판 2부터 **필수**입니다 — 없는 씬 파일은 읽지 않습니다.
 * 기본값으로 메우지 않는다: 스트리밍하지 않는 씬도 `_bStreaming="false"` 를 적는다.
 *
 * [스트리밍하는 씬]
 * 쿠커가 오브젝트 위치와 격자로 엔티티를 셀로 나누고(`SceneCooker`), 런타임은 스트리밍 원점 둘레의 셀을 올리고 내린다(`SceneStreamingSubsystem`).
 * 격자 · 내리는 여유 반경 · 프레임당 올리는 셀 수는 모두 이 데이터에서 온다. 코드에는 기본값이 없다 — 맞지 않으면 `validate` 가 오류로 멈춘다.
 *
 * [데이터 레이어]
 * 씬이 쓰는 레이어 이름과 처음 상태입니다(`_listDataLayer`, 언리얼 Data Layers). 엔티티는 `layers="Winter,Night"` 로 붙고, 켜진 레이어의 오브젝트만 오른다(`SceneDataLayerSubsystem`).
 * 적지 않은 씬은 레이어가 없다 — 이미 있는 씬은 그대로 읽힌다. 형식 판을 올리지 않는 까닭이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    /** @brief 씬이 선언한 데이터 레이어 하나입니다. 이름은 전역 이름 집합이다 — 인스턴스의 내부 씬이 같은 이름을 쓰면 같은 레이어다(`ResourceContext` 태그가 이 이름을 켤 수 있게). */
    REFLECT()
    struct SW_API SceneDataLayerDef
    {
        REFLECT_BODY();

        PROPERTY( InheritKey )
        string _name{};
        PROPERTY( Tooltip = "Objects of this layer load with the scene" )
        bool _bInitiallyActive{ false };
    };

    /**
     * @struct SceneSettings
     * @brief 파일 머리말 참고.
     */
    REFLECT()
    struct SW_API SceneSettings
    {
        REFLECT_BODY();

        /** @brief 씬 파일에서 이 설정을 담는 요소 이름입니다(리플렉션 타입 이름과 같다). */
        static constexpr const utf8* kElementName = "SceneSettings";

        PROPERTY( Tooltip = "Load this scene in cells around streaming sources instead of all at once" )
        bool _bStreaming{ false };
        PROPERTY( Tooltip = "Cell grid the cooker splits the scene with; used only when streaming" )
        StreamingCellGridDef _cellGrid{};
        PROPERTY( Min = 0.0, Tooltip = "A loaded cell is unloaded only when every source is farther than loading radius plus this", Units = m )
        float32 _unloadMargin{ 0.0f };
        PROPERTY( Min = 0, Tooltip = "Cells loaded per frame at most; a streaming scene needs at least 1" )
        uint32 _maxCellLoadsPerFrame{ 0 };
        PROPERTY( SkipIfEmpty, Tooltip = "Data layers objects of this scene can belong to (seasons, events, night)" )
        vector<SceneDataLayerDef> _listDataLayer{};

        /**
         * @brief 설정이 쓸 수 있는지 봅니다.
         * @details 데이터 레이어는 이름이 비지 않고 겹치지 않아야 합니다. 스트리밍하면 격자를 만들 수 있어야 하고(`StreamingCellGrid::tryCreate`), 프레임당 올리는 셀이 1 이상, 여유 반경이 0 이상이어야 합니다.
         *          어긋나면 씬 이름 @p sceneName 과 함께 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool validate( string_view sceneName ) const;
        /** @brief 이름 @p name 의 레이어를 선언했으면 그 정의입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const SceneDataLayerDef* findDataLayer( string_view name ) const;
    };
} // namespace sw
