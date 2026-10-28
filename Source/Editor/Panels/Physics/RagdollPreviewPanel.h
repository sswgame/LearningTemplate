/**
 * @file RagdollPreviewPanel.h
 * @brief 물리 에셋(`*.physics.xml`)을 고른 몸(프리팹)에 입혀 미리보기 씬에서 쓰러뜨려 보는 창입니다(언리얼 Physics Asset 에디터의 시뮬레이션, 유니티 Ragdoll Wizard 로 만든 래그돌 시험에 해당합니다).
 * @details 설계 결정: 상용 엔진과 같은 선에서 **보기 + 시험**만 한다. 언리얼 Physics Asset 에디터는 바디 · 제약을 편집 · 저장하고 유니티 Ragdoll Wizard 는 래그돌을 만든다 —
 *          이 엔진의 물리 에셋은 문서 종류(`Data` 의 `*.physics.xml`)라 카탈로그 편집기 · 인스펙터가 고치고, 이 창은 같은 파일을 쥐지 않도록 저장하지 않는다.
 *          창이 하는 일은 바디 표 + 뷰포트 겹침 선(스켈레톤 · 바디 셰이프 · 관절)과 시뮬레이션 시험(쓰러뜨리기 · 충격 · 부분 · 리셋)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class RagdollComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class RagdollPreviewPanel
     * @brief 연 물리 에셋을 미리보기 씬의 몸(프리팹 루트의 `SkeletalMeshComponent`)에 래그돌(`RagdollComponent`)로 걸고, 바디 · 관절을 보이며 물리 시험을 돌립니다.
     * @details **보기 전용 문서**다 — 물리 에셋은 이 창이 저장하지 않는다. 물리 에셋은 스켈레톤을 모르므로 입힐 몸(프리팹)을 사람이 고른다. 몸의 루트에 스켈레탈 메시가 있어야 하고
     *          에셋의 뼈 이름이 그 스켈레톤에 있어야 바디가 선다(없으면 래그돌 빌더가 오류를 남기고 아무것도 만들지 않는다). 몸에 `RagdollComponent` 가 없으면 창이 붙이고,
     *          있어도 물리 에셋 경로는 열린 문서로 바꾼다. 미리보기 정책은 틱 + 물리 + 플레이다(`getPreviewPolicy`) — 컴포넌트의 `onBeginPlay` 가 돌아야 바디가 선다.
     */
    class RagdollPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "ragdoll_preview";

        RagdollPreviewPanel();

        const utf8*        getPanelTitle() const override { return "Ragdoll Preview"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 문서에서 읽은 물리 에셋의 바디 수입니다. 문서를 읽지 못했으면 false 입니다(감시 값 `Editor.RagdollPreviewDocumentBodies`). */
        [[nodiscard]] bool findDocumentBodyCount( uint32& outCount ) const;
        /** @brief 미리보기 몸의 래그돌이 만든 바디 수입니다. 몸이 없거나 래그돌이 아직 서지 않았으면 false 입니다(감시 값 `Editor.RagdollPreviewBuiltBodies`). */
        [[nodiscard]] bool findBuiltBodyCount( uint32& outCount ) const;
        /** @brief 미리보기 몸의 래그돌 상태(`RagdollState` 의 번호)입니다. 몸이 없으면 false 입니다(감시 값 `Editor.RagdollPreviewState`). */
        [[nodiscard]] bool findRagdollState( uint32& outState ) const;

    protected:
        /** @brief 틱 + 물리 + 플레이, 고정 스텝은 촘촘히(관절 한계를 보려는 창이다). */
        ScenePreviewPolicy getPreviewPolicy() const override;
        /** @brief 바닥 면이 y = 0 이다(몸의 발이 닿는 높이). */
        EditorPreviewEnvironment getPreviewEnvironment() const override;

    private:
        /** @brief 문서의 바디 하나의 요약(표시용)입니다. 순서는 에셋 바디 순서이고 래그돌 바디 번호와 같다. */
        struct BodyRow
        {
            string           _bone{};
            string           _shapes{};
            string           _hitZone{};
            float32          _mass{ 0.0f };
            PhysicsJointType _jointType{ PhysicsJointType::Fixed };
        };

        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 정적 바닥 바디를 세운다. 몸은 "Spawn" 을 누르면 놓는다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 몸의 래그돌입니다. 몸이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        RagdollComponent* findBodyRagdoll() const;
        /** @brief 고른 몸을 놓고(경로나 문서가 바뀌었을 때만) 래그돌에 열린 물리 에셋을 겁니다. */
        void syncPreviewBody();
        void drawBodyTable( const RagdollComponent* pRagdoll );
        /** @brief 뷰포트 이미지 위에 스켈레톤 · 바디 셰이프 · 관절을 선으로 겹쳐 그립니다. */
        void drawOverlay( const RagdollComponent* pRagdoll );

        vector<BodyRow>  _listBodyRow;
        string           _bodyPrefabPath;    ///< 입력란의 몸 프리팹 경로(Spawn 을 눌러야 적용한다)
        string           _activePrefabPath;  ///< Spawn 으로 확정한 몸 프리팹 경로
        string           _spawnedPrefabPath; ///< 미리보기 씬에 놓은 몸의 프리팹 경로(비면 아직 놓지 않았다 — Reset 도 이것을 비운다)
        string           _spawnedAssetPath;  ///< 몸의 래그돌에 건 물리 에셋 경로
        string           _status;
        GameObjectHandle _body;
        float32          _impulse;
        int32            _selectedBody; ///< 표에서 고른 바디 번호(-1 이면 없음). 충격을 주는 바디다
        bool             _bDrawSkeleton;
        bool             _bDrawBodies;
        bool             _bDrawJoints;
    };
} // namespace sw::editor
