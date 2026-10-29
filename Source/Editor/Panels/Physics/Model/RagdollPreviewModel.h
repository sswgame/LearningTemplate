/**
 * @file RagdollPreviewModel.h
 * @brief 래그돌 미리보기 창의 모델 — 물리 에셋의 바디 표, 입힐 몸(프리팹) 경로, 미리보기 씬의 몸 놓기 · 래그돌 걸기, 충격 값 · 고른 바디 · 겹침 선 토글. ImGui 를 모릅니다.
 * @details 패널(`RagdollPreviewPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) 단추 · 표 · 겹침 선을 그리기만 한다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 Physics Asset 에디터의 툴킷 상태와 뷰포트 클라이언트가 나뉜 것과 같다). 씬은 패널이 넘긴다(모델은 씬을 갖지 않는다).
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class RagdollComponent;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 물리 에셋의 바디 하나의 요약(표시용)입니다. 순서는 에셋 바디 순서이고 래그돌 바디 번호와 같다. */
    struct RagdollPreviewBodyRow
    {
        string           _bone{};
        string           _shapes{};
        string           _hitZone{};
        float32          _mass{ 0.0f };
        PhysicsJointType _jointType{ PhysicsJointType::Fixed };
    };

    /**
     * @class RagdollPreviewModel
     * @brief 래그돌 미리보기 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class RagdollPreviewModel
    {
    public:
        /// @brief 충격의 처음 값(N*s)입니다.
        static constexpr float32 kDefaultImpulse = 120.0f;

        RagdollPreviewModel();

        /** @brief 물리 에셋 @p assetPath 를 읽어 바디 표를 다시 채웁니다. 고른 바디는 지웁니다. */
        ToolAssetLoadResult loadAsset( string_view assetPath );
        /** @brief 새 미리보기 씬 @p scene 에 정적 바닥 바디를 세웁니다. 새 씬에는 몸이 없으므로 놓은 몸을 잊습니다. */
        void populateScene( Scene& scene );

        /** @brief 입력란의 몸 경로를 확정하고 다음 `syncBody` 가 몸을 다시 놓게 합니다(Spawn 단추). */
        void spawnFromField();
        /** @brief 다음 `syncBody` 가 몸을 처음 자세로 다시 놓게 합니다(Reset · Reload). */
        void requestRespawn() { _spawnedPrefabPath.clear(); }
        /**
         * @brief 확정한 몸을 @p pScene 에 놓고(경로나 에셋이 바뀌었을 때만) 래그돌에 물리 에셋 @p assetPath 를 겁니다.
         * @details 에셋을 읽지 못했으면 부르는 쪽이 부르지 않는다. 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치고 Spawn 을 다시 누른다).
         */
        void syncBody( Scene* pScene, string_view assetPath );
        /** @brief @p pScene 에 놓은 몸의 래그돌입니다. 몸이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        [[nodiscard]] RagdollComponent* findRagdoll( Scene* pScene ) const;
        /** @brief 래그돌로 넘기고 고른 바디(없으면 첫 바디)를 충격 값만큼 밉니다(Impulse 단추). */
        void applyImpulse( RagdollComponent& ragdoll ) const;

        const vector<RagdollPreviewBodyRow>& getBodyRows() const { return _listBodyRow; }
        const string&                        getStatus() const { return _status; }
        /** @brief 입력란이 직접 고치는 몸 프리팹 경로입니다(Spawn 을 눌러야 적용한다). */
        string& editBodyPrefabPath() { return _bodyPrefabPath; }
        /** @brief 슬라이더가 직접 고치는 충격 값(N*s)입니다. */
        float32& editImpulse() { return _impulse; }
        /** @brief 표에서 고른 바디 번호(-1 이면 없음)입니다. 충격을 주는 바디다. */
        int32 getSelectedBody() const { return _selectedBody; }
        /** @brief 바디 @p bodyIndex 를 고릅니다. 이미 골랐으면 고름을 풉니다(표 행 클릭). */
        void toggleSelectedBody( int32 bodyIndex ) { _selectedBody = _selectedBody == bodyIndex ? -1 : bodyIndex; }

        bool& editDrawSkeleton() { return _bDrawSkeleton; }
        bool& editDrawBodies() { return _bDrawBodies; }
        bool& editDrawJoints() { return _bDrawJoints; }

    private:
        vector<RagdollPreviewBodyRow> _listBodyRow;
        string                        _bodyPrefabPath;    ///< 입력란의 몸 프리팹 경로(Spawn 을 눌러야 적용한다)
        string                        _activePrefabPath;  ///< Spawn 으로 확정한 몸 프리팹 경로
        string                        _spawnedPrefabPath; ///< 미리보기 씬에 놓은 몸의 프리팹 경로(비면 아직 놓지 않았다 — Reset 도 이것을 비운다)
        string                        _spawnedAssetPath;  ///< 몸의 래그돌에 건 물리 에셋 경로
        string                        _status;
        GameObjectHandle              _body;
        float32                       _impulse;
        int32                         _selectedBody; ///< 표에서 고른 바디 번호(-1 이면 없음)
        bool                          _bDrawSkeleton;
        bool                          _bDrawBodies;
        bool                          _bDrawJoints;
    };
} // namespace sw::editor
