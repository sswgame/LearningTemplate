/**
 * @file RigTargetGizmoEdit.h
 * @brief 리그 대상 오프셋 기즈모의 Model 쪽입니다 — 리그 디버그 창과 함께 보는 편집 상태(`RigTargetEdit`), 기즈모가 그릴 값 모으기, 기즈모(View)가 보낸 명령(끌기 · 놓기) 적용.
 * @details 리그 디버그 창이 대상을 고르고(`RigTargetEdit::_objectID` · `_targetIndex`), 시각화 "Rig Target"(View `Viewport/RigTargetGizmoVisualizer.cpp`)이 그 대상에 기즈모를 그린다.
 *          끄는 동안의 값은 `_pending` 이고, 놓으면 리그 파일에 되써 넣는다(`RigSaveParams::writeToRigFile`, 파일 글 전후가 되돌리기 하나) — 파일 감시가 다시 읽어 인스턴스에
 *          들어올 때까지 기즈모는 `_pending` 에 남는다. 창(패널 층)과 시각화(뷰포트 층)가 다른 층이라 상태는 아래 층인 여기서 에디터 범위의 상태 저장소가 소유한다.
 *          트랜스폼 기즈모(`EditorGizmoEdit`)와 같은 모양이다 — 언리얼 컴포넌트 비주얼라이저가 끌기를 트랜잭션으로 감싸는 자리. ImGui 를 모릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    class CameraComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 기즈모가 이번 프레임에 그릴 값입니다(`RigTargetEdit::gather`). */
    struct RigTargetGizmoViewState
    {
        float4x4 _view{};       ///< 활성 카메라의 뷰 행렬
        float4x4 _projection{}; ///< 캔버스 비율로 본 투영 행렬
        float4x4 _world{};      ///< 기즈모를 놓을 대상의 월드 행렬(남겨 둔 값이 있으면 그 값)
        bool     _bAvailable{ false };
        bool     _bRotate{ false };
        bool     _bDragging{ false };
    };

    /** @brief 기즈모가 보내는 명령의 종류입니다. */
    enum class RigTargetGizmoCommandKind : uint8
    {
        Drag,    ///< 끄는 중이다 — `_world` 를 대상의 부모 공간으로 옮겨 남겨 둔 값으로 둔다
        Release, ///< 놓았다 — 남겨 둔 값을 리그 파일에 되써 넣는다(되돌리기 하나)
    };

    /** @brief 기즈모 명령 하나입니다(값만 든다). */
    struct SW_EDITOR_API RigTargetGizmoCommand
    {
        RigTargetGizmoCommandKind _kind{ RigTargetGizmoCommandKind::Release };
        float4x4                  _world{}; ///< 끌기가 옮긴 월드 행렬(Drag 만)

        /**
         * @brief 기즈모 조작 한 프레임이 낼 명령의 순서(끌기 → 놓기)를 정합니다. ImGuizmo 결과 값만 받아 ImGui · 씬 없이 테스트한다.
         * @details @p pMovedWorld 가 있으면(이번 프레임에 옮겼다) 끌기를, 끌던 중(@p bDragging 또는 이번 끌기)인데 기즈모를 쥐고 있지 않으면(@p bUsing false) 놓기를 낸다.
         *          옮긴 프레임에 바로 놓아도 끌기 다음 놓기로 둘 다 낸다(예전 순서와 같다).
         */
        static void step( bool bDragging, const float4x4* pMovedWorld, bool bUsing, vector<RigTargetGizmoCommand>& outListCommand );
    };

    /** @brief 대상 오프셋 기즈모의 편집 상태입니다. 파일 머리말 참고. */
    struct SW_EDITOR_API RigTargetEdit
    {
        BoneTransform    _pending{};
        string           _status{};      ///< 지난 저장 결과 줄
        uint64           _objectID{ 0 }; ///< 기즈모를 놓은 대상의 오브젝트(0 이면 없음)
        optional<uint32> _targetIndex;   ///< 기즈모를 놓은 대상 번호(없으면 비어 있다)
        uint8            _bPending{ SW_FALSE };
        uint8            _bDragging{ SW_FALSE };

        /**
         * @brief 고른 대상의 기즈모 값을 @p outState 에 모읍니다(메인 씬). 놓은 값이 파일에서 다시 읽혔으면 남겨 둔 값을 버린다.
         * @details 대상이 있는 동안 뷰포트 마우스를 가져가(`EditorViewportMouseCapture`) 선택 기즈모 · 클릭 선택이 비킨다. 대상이 없으면 놓는다.
         *          @p pActiveCamera 가 없으면 대상이 없는 것과 같다.
         */
        void gather( const CameraComponent* pActiveCamera, float32 canvasAspect, RigTargetGizmoViewState& outState );
        /** @brief 기즈모 명령들을 차례로 적용합니다. 놓기는 남겨 둔 값을 리그 파일에 되써 넣는다(저장하지 못했으면 남겨 둔 값을 버려 기즈모가 파일 값으로 돌아온다). */
        void apply( const vector<RigTargetGizmoCommand>& listCommand );

        /** @brief 에디터 범위의 편집 상태입니다. */
        static RigTargetEdit& get();
    };
} // namespace sw::editor
