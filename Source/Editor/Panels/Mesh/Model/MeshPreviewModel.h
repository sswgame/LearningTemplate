/**
 * @file MeshPreviewModel.h
 * @brief 메시 미리보기 창의 모델 — 미리보기 씬의 메시 오브젝트 놓기 · 지금 문서 걸기, 읽힌 뒤 한 번 카메라를 맞출 경계 구, 고른 LOD(씬 단위 고정). ImGui 를 모릅니다.
 * @details 패널(`MeshPreviewPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) LOD 표를 그리기만 한다 — VFX · 래그돌 미리보기 `Model/` 과 같은 선이다
 *          (언리얼 Static Mesh 에디터의 툴킷 상태와 뷰포트가 나뉜 것과 같다). 씬은 패널이 넘긴다(모델은 씬을 갖지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class MeshComponent;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @class MeshPreviewModel
     * @brief 메시 미리보기 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class MeshPreviewModel
    {
    public:
        /// @brief "Auto"(화면 크기로 고른다)를 뜻하는 고른 단계 값입니다.
        static constexpr int32 kAutoLOD = -1;

        MeshPreviewModel();

        /** @brief 문서를 (다시) 읽었다 — 다음 `syncSubject` 가 메시를 다시 걸고 카메라를 다시 맞춘다. 메시는 컴포넌트가 비동기로 읽는다. */
        void resetForDocument();
        /** @brief 새 미리보기 씬 @p scene 에 메시 오브젝트 하나(`PreviewSubject`)를 스폰합니다. 메시 경로는 `syncSubject` 가 건다. */
        void populateScene( Scene& scene );
        /** @brief 다음 `syncSubject` 가 메시를 다시 걸게 합니다(다시 임포트한 메시 Reload). */
        void requestResync() { _previewMeshPath.clear(); }
        /**
         * @brief @p pScene 의 메시가 @p assetPath 를 그리게 하고(경로가 바뀌었을 때만), 메시가 읽혔으면 한 번 카메라를 맞출 경계 구를 냅니다.
         * @details 문서를 읽지 못했으면 부르는 쪽이 부르지 않는다. 메시를 읽지 못했으면 상태 줄에 남긴다.
         * @return 이번에 카메라를 맞춰야 하면 true 이고 @p outCenter · @p outRadius 에 경계 구(여유 포함)를 씁니다.
         */
        [[nodiscard]] bool syncSubject( Scene* pScene, string_view assetPath, float3& outCenter, float32& outRadius );
        /** @brief 고른 LOD 를 @p pScene 의 LOD 시스템에 고정합니다(Auto 면 화면 크기로 고른다). */
        void applyLODChoice( Scene* pScene ) const;
        /** @brief @p pScene 의 메시 컴포넌트입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        [[nodiscard]] static MeshComponent* findSubject( Scene* pScene );

        /** @brief 라디오 단추가 직접 고치는 고른 LOD 단계입니다(`kAutoLOD` = 화면 크기로 고른다). */
        int32&        editLODChoice() { return _lodChoice; }
        const string& getStatus() const { return _status; }

    private:
        string _previewMeshPath; ///< 미리보기 메시에 건 경로(비면 다음 `syncSubject` 가 다시 건다)
        string _status;
        int32  _lodChoice; ///< 고른 LOD 단계(-1 = Auto, 화면 크기로 고른다)
        bool   _bFramed;   ///< 읽힌 메시로 카메라를 한 번 맞췄다
    };
} // namespace sw::editor
