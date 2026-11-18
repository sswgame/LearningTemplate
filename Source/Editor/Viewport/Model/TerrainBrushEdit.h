/**
 * @file TerrainBrushEdit.h
 * @brief 지형 칠하기 도구의 Model 쪽입니다 — 커서 레이로 지형을 맞추고 브러시 원 값을 모으며, 도구(View)가 보낸 명령(붓질 시작 · 적용 · 끝, 크기 바꾸기, 저장)을 지형에 적용합니다. ImGui 를 모릅니다.
 * @details 드래그를 시작하면 그 지형의 높이(또는 스플랫 가중치 · 구멍) 전부를 한 번 복사해 두고, 프레임마다 사본에 브러시(`TerrainBrush`)를 적용해 바뀐 사각형만 지형에 쓴다.
 *          놓으면 드래그 전체가 닿은 사각형의 전 · 후 값만 담아 되돌리기 하나(`Sculpt Terrain` · `Paint Terrain` · `Paint Terrain Holes`)를 남긴다 — 언리얼 Landscape Mode 의 붓질 트랜잭션 자리.
 *          쓴 값은 에셋 파일에 들어가지 않는다. 저장 명령이 원래 경로에 쓴다. 도구는 시각화 등록 줄의 정적 함수라 상태는 에디터 범위의 상태 저장소가 든다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/Model/TerrainBrush.h"

namespace sw::editor
{
    /** @brief 도구가 이번 프레임에 그릴 값입니다(`TerrainBrushEdit::gather`). */
    struct TerrainBrushViewState
    {
        float3         _hit{};                       ///< 커서 레이가 지형에 닿은 점
        vector<float3> _listCircle;                  ///< 브러시 반경 원(지형 높이를 따라 띄운 꺾은선, 월드)
        vector<float3> _listInnerCircle;             ///< 감쇠가 시작되는 안쪽 원(없으면 비었다)
        uint32         _currentResolution{ 0 };      ///< 크기 바꾸기 대상 지형의 해상도(대상이 없으면 0)
        uint32         _resizeResolution{ 0 };       ///< 설정 창이 고른 새 해상도
        bool           _bHit{ false };               ///< NOLINT(sw-bool-flag): 값 집합체 — 커서 레이가 지형에 닿았다
        bool           _bEditable{ false };          ///< NOLINT(sw-bool-flag): 값 집합체 — 씬 편집이 허용된다(플레이 중이면 막힌다)
        bool           _bStroking{ false };          ///< NOLINT(sw-bool-flag): 값 집합체 — 붓질 중이다
        bool           _bStrokeTargetAlive{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 붓질 중인 지형이 아직 있다
        bool           _bCanSave{ false };           ///< NOLINT(sw-bool-flag): 값 집합체 — 마지막으로 고친 지형이 있다
        bool           _bUnsaved{ false };           ///< NOLINT(sw-bool-flag): 값 집합체 — 저장하지 않은 편집이 있다
    };

    /** @brief 도구가 보내는 명령의 종류입니다. */
    enum class TerrainBrushCommandKind : uint8
    {
        Begin,  ///< `_hit` 아래 지형의 붓질을 시작한다(지형 값 전부를 사본으로 둔다)
        Apply,  ///< 붓질 중인 지형의 `_hit` 에 이번 프레임의 브러시를 적용한다(엔진 프레임 경과만큼)
        End,    ///< 붓질을 마친다 — 바뀐 것이 있으면 되돌리기 하나를 남긴다
        Resize, ///< 크기 바꾸기 대상을 `_resolution` 으로 다시 표본화한다(되돌리기 `Resize Terrain`)
        Save,   ///< 마지막으로 고친 지형의 높이장 · 스플랫을 원래 에셋 경로에 쓴다
    };

    /** @brief 도구 명령 하나입니다(값만 든다). */
    struct TerrainBrushCommand
    {
        TerrainBrushCommandKind _kind{ TerrainBrushCommandKind::End };
        float3                  _hit{};
        uint32                  _resolution{ 0 };
    };

    /** @brief 한 프레임의 붓질 입력입니다(`TerrainBrushEdit::stepStroke`). */
    struct TerrainBrushStrokeInput
    {
        float3 _hit{};
        bool   _bUsable{ false };    ///< NOLINT(sw-bool-flag): 값 집합체 — 씬 뷰가 도구를 쓸 수 있다(3D 뷰 · 씬이 있다)
        bool   _bStroking{ false };  ///< NOLINT(sw-bool-flag): 값 집합체 — 붓질 중이다
        bool   _bStillDown{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 단추를 누르고 있고 편집이 허용되며 붓질 중인 지형이 있다
        bool   _bHit{ false };       ///< NOLINT(sw-bool-flag): 값 집합체 — 커서 레이가 지형에 닿았다
        bool   _bPressed{ false };   ///< NOLINT(sw-bool-flag): 값 집합체 — 도구가 마우스를 가진 채 이번 프레임에 왼쪽 단추를 눌렀다
    };

    /**
     * @struct TerrainBrushEdit
     * @brief 붓질 · 크기 바꾸기 · 저장의 적용과 기록입니다(메인 씬, 메인 스레드).
     */
    struct SW_EDITOR_API TerrainBrushEdit
    {
        /** @brief 레이 원 하나를 이루는 선분 수입니다. */
        static constexpr uint32 kCircleSegment = 48;

        /** @brief 커서 레이 @p pRay(캔버스 밖이면 nullptr)로 지형을 맞추고 브러시 원 · 설정 창 값을 모읍니다. 맞은 지형은 크기 바꾸기 대상의 다음 후보로 남긴다. */
        static void gather( const Ray* pRay, TerrainBrushViewState& outState );
        /** @brief 붓질 명령 순서를 정합니다 — 쓸 수 없으면 끝, 붓질 중이면 놓았을 때 끝 · 닿았으면 적용, 아니면 누름이 시작 + 적용이다(ImGui · 씬 없이 테스트한다). */
        static void stepStroke( const TerrainBrushStrokeInput& input, vector<TerrainBrushCommand>& outListCommand );
        /** @brief 명령들을 차례로 적용합니다. */
        static void apply( const vector<TerrainBrushCommand>& listCommand );
        /** @brief 브러시 설정입니다(설정 창과 개발 명령 `terrain.brush` 가 고칩니다). */
        static TerrainBrushSettings& getSettings();
        /** @brief 설정 창이 새 해상도를 골랐습니다. */
        static void setResizeResolution( uint32 resolution );
        /** @brief 크기 바꾸기 대상이 받는 해상도(청크 칸의 배수 + 1)를 모읍니다. 대상이 없으면 비었다. */
        static void collectUsableResolutions( vector<uint32>& outListResolution );
        /** @brief 도구가 UI 프레임 @p uiFrame 에 돌았다고 적습니다(감시 값 `Editor.TerrainBrushToolActive` — 프레임 번호는 View 가 준다). */
        static void markActive( int32 uiFrame );
        /** @brief 도구가 마지막으로 돈 UI 프레임입니다. 돈 적이 없으면 비어 있다. */
        static optional<int32> getLastActiveFrame();
    };
} // namespace sw::editor
