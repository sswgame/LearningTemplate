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
 * 격자 · 내리는 여유 반경 · 프레임당 읽기 시작하는 셀 수 · 짓기 시간 예산은 모두 이 데이터에서 온다. 코드에는 기본값이 없다 — 맞지 않으면 `validate` 가 오류로 멈춘다.
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
        PROPERTY( Min = 0, Tooltip = "Cells whose content read starts per frame at most; a streaming scene needs at least 1" )
        uint32 _maxCellLoadsPerFrame{ 0 };
        /**
         * @brief 한 프레임에 셀 오브젝트를 짓는 데 쓰는 게임 스레드 시간(마이크로초)입니다. 넘으면 남은 셀은 다음 프레임에 잇는다(언리얼 `s.LevelStreamingActorsUpdateTimeLimit` — AddToWorld 시간 예산).
         * @details 프레임마다 셀 하나는 예산과 상관없이 짓는다(멈추지 않게). 스트리밍하는 씬은 0 이면 `validate` 가 막는다 — 코드에 기본값이 없다.
         *          엔진 설정이 아니라 월드 설정에 두는 까닭: 셀 크기 · 셀당 오브젝트 수가 월드마다 달라 같은 예산이 맞지 않는다(격자 · 여유 반경과 같은 자리).
         */
        PROPERTY( Min = 0, Meta = "Units=us", Tooltip = "Game thread time per frame for adding loaded cells to the world; a streaming scene needs a positive value" )
        uint32 _addToWorldBudgetMicroseconds{ 0 };
        PROPERTY( SkipIfEmpty, Tooltip = "Data layers objects of this scene can belong to (seasons, events, night)" )
        vector<SceneDataLayerDef> _listDataLayer{};
        /**
         * @brief 이 씬을 씬 인스턴스로 넣거나 하위 레벨로 더할 때 쿠킹이 무엇을 묶는지입니다(언리얼 Packed Level Actor). 비면 묶지 않는다 — 이미 있는 씬은 그대로 읽힌다.
         * @details `StaticMeshes`(`kPackStaticMeshes`) 면 쿠커가 이 씬의 정적 메시 엔티티(메시 컴포넌트 하나뿐 · `_bStatic` · 아무도 가리키지 않음)를 메시 · 머티리얼 · 구간마다
         *          인스턴스 메시 하나로 묶는다(`SceneCooker::packStaticMeshes`). 에디터 · 소스 실행은 원본 엔티티를 그대로 짓는다. 스트리밍하는 씬은 묶지 않는다(`validate` 가 막는다).
         *          글 칸인 까닭: 빈 값만 쓰지 않는 칸(`SkipIfEmpty`)이라야 묶지 않는 씬 파일이 바뀌지 않는다 — 모르는 값은 `validate` 가 오류로 멈춘다.
         */
        PROPERTY( SkipIfEmpty, Tooltip = "What the cooker packs when this scene is placed as an instance or added as a sub-level: empty (nothing) or StaticMeshes" )
        string _packing{};

        /**
         * @brief 게임 카메라가 월드 원점에서 이만큼(XZ, m) 멀어지면 원점을 카메라 쪽으로 옮깁니다(언리얼 World Origin Rebasing · 유니티 floating origin). 0 이면 옮기지 않는다.
         * @details 옮기는 거리는 이 값의 배수다 — 셀 격자 · 정적 그림자 캐시가 덜 흔들린다. 바깥 원점으로 스트리밍하는 씬(서버 — 관찰자가 여럿)은 옮기지 않는다(서버 좌표는 절대).
         *          월드 설정에 두는 까닭: 월드 크기마다 맞는 거리가 다르다(격자 · 여유 반경과 같은 자리).
         */
        PROPERTY( Min = 0.0, Units = m, Tooltip = "The world origin moves to the game camera once it is this far away (XZ); 0 never moves it" )
        float32 _originRebaseDistance{ 0.0f };

        /**
         * @brief 셀 조각의 HLOD 대리에서, 한 조각에 이 수 이하로 놓인 메시는 머티리얼마다 메시 하나로 합칩니다(`HLODMeshComponent` — 언리얼 HLOD 레이어 `MeshMerge`). 더 많이 놓인 메시는 인스턴스로 둔다(`Instancing`).
         * @details 0 이면 합치지 않는다(모두 인스턴스). 합치면 그리기가 (메시, 머티리얼)마다에서 머티리얼마다로 줄고 정점은 놓인 수만큼 는다 — 여러 벌 놓인 메시는 인스턴스가 싸다.
         *          월드 설정에 두는 까닭: 셀 크기 · 셀당 메시 수가 월드마다 다르다(격자 · 여유 반경과 같은 자리).
         */
        PROPERTY( Min = 0, Tooltip = "HLOD proxies merge meshes placed at most this many times in a cell into one mesh per material; meshes placed more often stay instanced (0 merges nothing)" )
        uint32 _hlodMergeMaxInstanceCount{ 0 };

        /** @brief `_packing` 이 정적 메시를 묶는다는 값입니다. */
        static constexpr const utf8* kPackStaticMeshes = "StaticMeshes";
        /** @brief 쿠킹이 정적 메시를 묶으면 true 입니다. */
        bool usesStaticMeshPacking() const { return _packing == kPackStaticMeshes; }

        /**
         * @brief 설정이 쓸 수 있는지 봅니다.
         * @details 데이터 레이어는 이름이 비지 않고 겹치지 않아야 합니다. 스트리밍하면 격자를 만들 수 있어야 하고(`StreamingCellGrid::tryCreate`), 프레임당 읽는 셀이 1 이상, 짓기 예산이 0 보다 크고, 여유 반경이 0 이상이어야 합니다.
         *          어긋나면 씬 이름 @p sceneName 과 함께 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool validate( string_view sceneName ) const;
        /** @brief 이름 @p name 의 레이어를 선언했으면 그 정의입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const SceneDataLayerDef* findDataLayer( string_view name ) const;
    };
} // namespace sw
