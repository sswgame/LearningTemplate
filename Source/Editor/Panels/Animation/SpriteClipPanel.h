#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/GUI/EditorThumbnailCache.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"

namespace sw::editor
{
    /**
     * @brief 프레임 목록 · 이름 붙은 애니메이션 구간 · 선택적 TransformAnimation 키를 편집합니다 (AnimationGraph과 별개)
     * @details 문서는 런타임 타입 `SpriteClipAsset` 하나입니다 — 게임이 읽는 것과 같은 파서 · 같은 구조체를 편집합니다.
     */
    class SpriteClipPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "sprite_clip";

        /** @brief 스프라이트 클립 도구를 생성합니다. */
        SpriteClipPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 스프라이트 클립 편집 UI를 그립니다. */
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        /** @brief 아틀라스 이미지를 해제합니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;

    private:
        /** @brief 아틀라스 경로 입력, 불러오기 · 저장 단추입니다. */
        void drawFileBar();
        /** @brief 프레임 목록과 고른 프레임의 uv · 지속 시간 칸입니다. */
        void drawFrameSection();
        /** @brief 변형 키 목록과 고른 키의 시간 · 위치 · 각도 칸입니다. */
        void drawKeySection();
        /** @brief 실수 드래그 필드 하나입니다. 손을 떼며 값이 바뀌었으면 편집으로 기록합니다. @p pTooltip 이 nullptr 이면 툴팁이 없습니다. */
        void            drawEditedDragFloat( const utf8* pLabel, float32* pValue, float32 speed, const utf8* pTooltip, const utf8* pUndoLabel, const utf8* pMergeKey );
        string          captureDocumentText() const override;
        void            applyDocumentText( string_view text ) override;
        SpriteClipAsset captureClipData() const;
        /** @brief 읽은(또는 되돌린) 클립으로 편집 상태를 바꿉니다. 아틀라스가 비어 있으면 지금 아틀라스를 그대로 둡니다. */
        void adoptClip( SpriteClipAsset&& clip );
        /** @brief 이름 붙은 애니메이션 구간 목록을 그립니다. */
        void drawAnimationSection();
        /**
         * @brief 아틀라스 이미지를 열 · 행 셀로 나눠 보이고, 누른 셀을 고른 프레임의 UV 로 넣습니다(Shift 는 새 프레임으로 더한다).
         * @details 유니티 Sprite Editor 의 Slice by Cell Count 와 같다. 셀 계산은 `AtlasGridUtil`.
         */
        void drawAtlasPicker();

    private:
        // ------------------------------------------------------------------------------
        // 2) 프레임 · 트랜스폼 키
        // ------------------------------------------------------------------------------
        using Frame        = SpriteClipFrame;
        using TransformKey = SpriteClipKey;
        using Animation    = SpriteClipAnimation;

        // ------------------------------------------------------------------------------
        // 3) SpriteClip.xml 로드/저장
        // ------------------------------------------------------------------------------
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief SpriteClip.xml을 저장합니다. */
        void saveClip();

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
        int32                                 _sliceColumns; ///< 아틀라스를 나눌 열 수(셀 고르기)
        int32                                 _sliceRows;    ///< 아틀라스를 나눌 행 수
        EditorThumbnailCache                  _atlasThumbnail;
    };
} // namespace sw::editor
