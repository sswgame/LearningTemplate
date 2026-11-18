/**
 * @file PairedAnimationPreviewDriver.h
 * @brief 페어드 애니메이션 창의 미리보기 씬 동기화 — 역할마다 프리팹을 놓고, 기즈모가 옮긴 오프셋을 모델에 넣고, 스크럽 시각의 자세 · 자리를 맞춥니다.
 * @details 씬 · 오브젝트 · 애니메이터는 이 파일의 `.cpp` 만 본다(창 View 는 씬 헤더를 보지 않는다 — `docs/agents/EditorUI.md` MVVM). 씬은 창(`EditorPreviewPanel`)이 쥐고 넘긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    class GameObjectManager;
    class Scene;
    class SkeletalAnimatorComponent;
} // namespace sw

namespace sw::editor
{
    class PairedAnimationEditModel;

    /** @brief 역할 하나의 미리보기 오브젝트입니다(역할 인덱스와 같은 순서). */
    struct PairedAnimationRoleSlot
    {
        fixed_string<constant::file::kMaxPathSize> _prefabPath{};     ///< 미리보기에 놓을 프리팹(리소스 id). 비면 놓지 않는다
        string                                     _spawnedPath{};    ///< 지금 놓은 오브젝트의 프리팹. 다르면 다시 놓는다
        GameObjectHandle                           _object{};         ///< 놓은 오브젝트
        float3                                     _placedPosition{}; ///< 지난 그리기에 창이 놓은 자리 — 다르면 기즈모가 옮긴 것이다
        float32                                    _placedYaw{ 0.0f };
        float32                                    _clipSeconds{ 0.0f }; ///< 역할 클립 길이(못 읽었으면 0)
    };

    /**
     * @class PairedAnimationPreviewDriver
     * @brief 역할 미리보기 오브젝트 목록과 그 동기화입니다. 파일 머리말 참고.
     */
    class PairedAnimationPreviewDriver
    {
    public:
        /** @brief 역할마다 한 칸입니다(창이 프리팹 경로를 고친다). */
        vector<PairedAnimationRoleSlot>&       editSlots() { return _listSlot; }
        const vector<PairedAnimationRoleSlot>& getSlots() const { return _listSlot; }
        /** @brief 새 미리보기 씬이 섰다 — 놓은 오브젝트를 잊습니다(옛 씬과 함께 사라졌다). */
        void clearSpawned();
        /** @brief 놓은 오브젝트를 @p pScene 에서 지우고 잊습니다(파일을 다시 읽어 역할 수가 바뀌었을 수 있다). */
        void destroySpawned( Scene* pScene );
        /**
         * @brief 역할 프리팹을 (다시) 놓고, 기즈모가 옮긴 오프셋을 @p model 에 넣고, 스크럽 시각의 자세 · 자리를 맞춥니다. 씬이 없으면 아무것도 하지 않습니다.
         * @param inoutStatus 프리팹 · 클립을 못 읽었을 때 이유를 씁니다.
         * @param outEdited 기즈모가 옮긴 오프셋을 모델에 넣었으면 true 이고, 그 결과가 @p outEdit 입니다.
         */
        void sync( Scene* pScene, PairedAnimationEditModel& model, bool bGizmoHasMouse, string& inoutStatus, bool& outEdited, expected<void>& outEdit );

    private:
        /** @brief 역할의 미리보기 오브젝트를 놓습니다(프리팹이 바뀌었으면 옛 것을 지우고). */
        void ensureRoleObject( GameObjectManager& manager, const PairedAnimationEditModel& model, uint32 roleIndex, string& inoutStatus );
        /** @brief 역할 오브젝트의 애니메이터를 스크럽 시각의 자세로 맞춥니다. */
        void poseRoleObject( SkeletalAnimatorComponent& animator, const PairedAnimationEditModel& model, uint32 roleIndex ) const;

    private:
        vector<PairedAnimationRoleSlot> _listSlot;
    };
} // namespace sw::editor
