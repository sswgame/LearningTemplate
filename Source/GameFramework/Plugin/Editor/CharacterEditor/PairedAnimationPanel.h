/**
 * @file PairedAnimationPanel.h
 * @brief 페어드 애니메이션(`*.pairedanim.xml`) 편집 창입니다 — 미리보기 씬에 역할마다 프리팹을 놓고 한 시계로 스크럽하며, 정렬 오프셋을 기즈모로 옮기고,
 *        공유 알림의 시각 · 역할을 고칩니다.
 * @details 데이터 편집은 모두 `PairedAnimationEditModel` 이 하고 이 창은 그리기와 미리보기 씬 동기화만 한다. 저장은 모델이 규칙(런타임 검사)을 통과할 때만 된다.
 *          - **미리보기 배치**: 앵커는 원점 · 요 0 이다. 정렬 역할(`snap` · `warp`)은 정렬 목표(`computeRoleTarget`)에, 정렬하지 않는 역할(부착 프롭)은 원점에 놓는다.
 *            미리보기는 부착을 하지 않는다 — 활 같은 프롭은 런타임에 손 소켓에 붙는다.
 *          - **스크럽**: 역할 오브젝트의 애니메이터를 재생 속도 0 으로 두고, 단계마다 역할 클립(반복 없음)을 `seekCurrentTime` 으로 옮기거나(재생 중),
 *            종료 상태를 재생한다(나감). 시각 계산은 세션과 같은 규칙이다(모델).
 *          - **오프셋 기즈모**: 창 선택에 놓인 정렬 역할 오브젝트를 기즈모로 옮기면, 다음 그리기에서 그 자리를 앵커 기준 오프셋 · 요로 바꿔 모델에 넣는다(연속 편집).
 *          - 역할 프리팹 경로는 이 창의 미리보기 설정이라 에셋에 저장하지 않는다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Viewport/EditorPreviewGizmo.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/PairedAnimationEditModel.h"

namespace sw
{
    class GameObjectManager;
    class SkeletalAnimatorComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class PairedAnimationPanel
     * @brief 페어드 애니메이션 편집 창입니다. 파일 머리말 참고.
     */
    class PairedAnimationPanel final : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "charactereditor.pairedanimation";

        PairedAnimationPanel();

        const utf8*        getPanelTitle() const override { return "Paired Animation"; }
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 120.0f, 40.0f ); }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override { return isDocumentLoadFailed() == false && _model.isDirty(); }

        const PairedAnimationEditModel& getModel() const { return _model; }

    private:
        /** @brief 역할 하나의 미리보기 오브젝트입니다(역할 인덱스와 같은 순서). */
        struct RoleSlot
        {
            fixed_string<constant::kMaxPathSize> _prefabPath{};     ///< 미리보기에 놓을 프리팹(리소스 id). 비면 놓지 않는다
            string                               _spawnedPath{};    ///< 지금 놓은 오브젝트의 프리팹. 다르면 다시 놓는다
            GameObjectHandle                     _object{};         ///< 놓은 오브젝트
            float3                               _placedPosition{}; ///< 지난 그리기에 창이 놓은 자리 — 다르면 기즈모가 옮긴 것이다
            float32                              _placedYaw{ 0.0f };
            float32                              _clipSeconds{ 0.0f }; ///< 역할 클립 길이(못 읽었으면 0)
        };

        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        void                populatePreviewScene( Scene& scene ) override;

        void drawTimeline();
        void drawRoleRows();
        void drawNotifyRows();
        void drawHistoryRow();
        /** @brief 역할 프리팹을 (다시) 놓고, 기즈모가 옮긴 오프셋을 모델에 넣고, 스크럽 시각의 자세 · 자리를 맞춥니다. */
        void syncPreviewRoles();
        /** @brief 역할의 미리보기 오브젝트를 놓습니다(프리팹이 바뀌었으면 옛 것을 지우고). */
        void ensureRoleObject( GameObjectManager& manager, uint32 roleIndex );
        /** @brief 역할 오브젝트의 애니메이터를 스크럽 시각의 자세로 맞춥니다. */
        void poseRoleObject( SkeletalAnimatorComponent& animator, uint32 roleIndex );
        /** @brief 모델 편집 결과를 상태 줄과 dirty 표시에 반영합니다. */
        void reportEdit( bool bSucceeded );
        /** @brief 미리보기 씬의 매니저입니다. 씬이 없으면 nullptr 입니다. */
        GameObjectManager* findPreviewManager() const;

        PairedAnimationEditModel             _model;
        EditorPreviewGizmo                   _gizmo;
        vector<RoleSlot>                     _listSlot;
        vector<uint32>                       _listFiredNotify; ///< 지난 스크럽이 앞으로 지나며 울린 알림(표시용)
        string                               _status;
        string                               _editError;
        fixed_string<constant::kMaxBuffer64> _newNotifyName;
        float32                              _previousScrubTime;
    };
} // namespace sw::editor
