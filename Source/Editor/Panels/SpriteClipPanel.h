#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Engine/Animation/SpriteClipAsset.h"

namespace sw::editor
{
    /**
     * @brief 프레임 목록 · 이름 붙은 애니메이션 구간 · 선택적 TransformAnimation 키를 편집합니다 (AnimGraph과 별개)
     * @details 문서는 런타임 타입 `SpriteClipAsset` 하나입니다 — 게임이 읽는 것과 같은 파서 · 같은 구조체를 편집합니다.
     */
    class SpriteClipPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 스프라이트 클립 도구를 생성합니다. */
        SpriteClipPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 스프라이트 클립 편집 UI를 그립니다. */
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;

    private:
        string          captureDocumentText() const override;
        void            applyDocumentText( string_view text ) override;
        SpriteClipAsset captureClipData() const;
        /** @brief 읽은(또는 되돌린) 클립으로 편집 상태를 바꿉니다. 아틀라스가 비어 있으면 지금 아틀라스를 그대로 둡니다. */
        void adoptClip( SpriteClipAsset&& clip );
        /** @brief 이름 붙은 애니메이션 구간 목록을 그립니다. */
        void drawAnimationSection();

    private:
        // ------------------------------------------------------------------------------
        // 2) 프레임 · 트랜스폼 키
        // ------------------------------------------------------------------------------
        using Frame        = SpriteClipFrame;
        using TransformKey = SpriteClipKey;
        using Animation    = SpriteClipAnimation;

        // ------------------------------------------------------------------------------
        // 3) SpriteClip.json 로드/저장
        // ------------------------------------------------------------------------------
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief SpriteClip.json을 저장합니다. */
        void saveJson();

    private:
        fixed_string<constant::kMaxBuffer256> _atlasPath;
        string                                _status;
        vector<Frame>                         _listFrame;
        vector<TransformKey>                  _listKey;
        vector<Animation>                     _listAnimation;
        fixed_string<constant::kMaxBuffer64>  _animationName; ///< 고른 애니메이션 이름의 편집 칸입니다
        int32                                 _selectedFrame;
        int32                                 _selectedKey;
        int32                                 _selectedAnimation;
    };
} // namespace sw::editor
