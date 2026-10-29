/**
 * @file VFXPreviewModel.h
 * @brief VFX 미리보기 창의 모델 — 효과 파일의 방출기 표, 미리보기 씬의 효과 오브젝트 놓기 · 지금 문서 재생 걸기. ImGui 를 모릅니다.
 * @details 패널(`VFXPreviewPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) 단추 · 목록을 그리기만 한다 — 래그돌 미리보기(`RagdollPreviewModel`)와 같은 선이다
 *          (언리얼 Niagara 에디터의 시스템 툴킷 상태와 뷰포트가 나뉜 것과 같다). 씬은 패널이 넘긴다(모델은 씬을 갖지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw
{
    class Scene;
    class VFXComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 방출기 하나의 요약(목록 표시용)입니다. */
    struct VFXPreviewEmitterRow
    {
        string _name{};
        string _texturePath{};
        uint32 _maxParticles{ 0 };
        bool   _bAdditive{ false };
        bool   _bLooping{ false };
    };

    /**
     * @class VFXPreviewModel
     * @brief VFX 미리보기 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class VFXPreviewModel
    {
    public:
        VFXPreviewModel();

        /** @brief 효과 파일 @p assetPath 를 읽어 방출기 표를 다시 채우고, 다음 `syncEffect` 가 효과를 다시 걸게 합니다. */
        ToolAssetLoadResult loadAsset( string_view assetPath );
        /** @brief 새 미리보기 씬 @p scene 에 효과 오브젝트 하나(`PreviewSubject`)를 스폰합니다. 효과 파일은 `syncEffect` 가 건다. */
        void populateScene( Scene& scene );
        /** @brief 다음 `syncEffect` 가 효과를 다시 걸게 합니다(Reload). */
        void requestResync() { _previewEffectPath.clear(); }
        /**
         * @brief @p pScene 의 효과가 효과 파일 @p assetPath 를 재생하게 합니다(경로가 바뀌었거나 다시 읽었을 때만).
         * @details 문서를 읽지 못했으면 부르는 쪽이 부르지 않는다. 미리보기 씬은 플레이하지 않으므로(틱만) 재생은 여기서 시작한다.
         */
        void syncEffect( Scene* pScene, string_view assetPath );
        /** @brief @p pScene 의 효과입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        [[nodiscard]] static VFXComponent* findSubject( Scene* pScene );

        const vector<VFXPreviewEmitterRow>& getEmitterRows() const { return _listEmitterRow; }
        const string&                       getStatus() const { return _status; }

    private:
        vector<VFXPreviewEmitterRow> _listEmitterRow;
        string                       _previewEffectPath; ///< 미리보기 효과에 건 효과 경로(비면 다음 `syncEffect` 가 다시 건다)
        string                       _status;
    };
} // namespace sw::editor
