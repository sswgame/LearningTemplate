#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Panels/Animation/Model/SpriteClipModel.h"

namespace sw::editor
{
    /**
     * @brief 프레임 목록 · 이름 붙은 애니메이션 구간 · 선택적 TransformAnimation 키를 편집합니다 (AnimationGraph과 별개)
     * @details 문서는 런타임 타입 `SpriteClipAsset` 하나입니다 — 게임이 읽는 것과 같은 파서 · 같은 구조체를 편집합니다.
     *          문서 상태와 명령은 `SpriteClipModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기 · 아틀라스 썸네일 · 문서 dirty · 되돌리기 기록만 맡는다.
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
        /** @brief 포커스한 이미지가 아니라 곁 클립 파일(`SpriteClipModel::computeClipPath`)에 쓴다. */
        void collectFilesToSave( vector<string>& outListPath ) const override;
        /** @brief 아틀라스 이미지를 해제합니다. */
        void shutdown() override;
        /** @brief 문서를 읽었습니다(감시 값이 읽지 않은 창을 거른다). */
        bool isClipDocumentLoaded() const { return isDocumentLoaded(); }
        /** @brief 문서의 프레임 수입니다(감시 값 `Editor.SpriteClipFrameCount`). */
        uint32 getFrameCount() const { return static_cast<uint32>( _model.getFrames().size() ); }
        /** @brief 문서의 이름 붙은 애니메이션 구간 수입니다(감시 값 `Editor.SpriteClipAnimationCount`). */
        uint32 getAnimationCount() const { return static_cast<uint32>( _model.getAnimations().size() ); }
        /** @brief 창의 모델입니다(감시 값이 고른 프레임의 크기를 읽는다). */
        const SpriteClipModel& getModel() const { return _model; }

    private:
        /** @brief 아틀라스 경로 입력, 불러오기 · 저장 단추입니다. */
        void drawFileBar();
        /** @brief 프레임 목록과 고른 프레임의 uv · 지속 시간 칸입니다. */
        void drawFrameSection();
        /** @brief 변형 키 목록과 고른 키의 시간 · 위치 · 각도 칸입니다. */
        void drawKeySection();
        /** @brief 이름표 열이 있는 실수 드래그 필드 하나입니다. 손을 떼며 값이 바뀌었으면 편집으로 기록합니다. @p pTooltip 이 nullptr 이면 툴팁이 없습니다. */
        void drawEditedDragFloat( const utf8* pLabel, float32* pValue, float32 speed, const utf8* pTooltip, const utf8* pUndoLabel );
        /** @brief 고른 프레임의 9-슬라이스 픽셀 테두리 칸입니다(유니티 Sprite Border). */
        void drawFrameBorderProperties( SpriteClipFrame& frame );
        /** @brief 고른 구간(없으면 프레임 전체)을 돌려 보는 미리보기입니다(Godot SpriteFrames 의 재생). */
        void   drawPreviewSection();
        string captureDocumentText() const override;
        void   applyDocumentText( string_view text ) override;
        /** @brief 이름 붙은 애니메이션 구간 목록을 그립니다. */
        void drawAnimationSection();
        /**
         * @brief 아틀라스 이미지를 열 · 행 셀로 나눠 보이고, 누른 셀을 고른 프레임의 UV 로 넣습니다(Shift 는 새 프레임으로 더한다).
         * @details 유니티 Sprite Editor 의 Slice by Cell Count 와 같다. 셀 계산은 `SpriteClipModel::pickAtlasCell`(`AtlasGridUtil`).
         */
        void drawAtlasPicker();
        /**
         * @brief 아틀라스 위 끌기입니다 — 고른 프레임의 가장자리 가까이에서 누르면 그 가장자리를, 아니면 새 사각형을 끈다(불규칙 프레임). 문턱 안에서 떼면 셀 고르기입니다.
         * @details 끄는 동안 가장자리는 바로 따라오고(미리보기 · 런타임과 같은 uv), 되돌리기는 뗄 때 한 번 남긴다. 사각형은 뗄 때 프레임이 된다(Shift 는 새 프레임).
         */
        void handleAtlasPointer( const float2& imageMin, float32 side, bool bHovered );
        /** @brief 고른 프레임 가장자리의 화면 자리(자동화 표시 `spriteClip.frameEdge.<left|top|right|bottom>`)를 적고, @p mouse 가 가까운 가장자리를 돌려줍니다. 없으면 비어 있습니다. */
        optional<SpriteFrameEdge> findFrameEdgeNear( const float2& imageMin, float32 side, const float2& mouse ) const;

        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;

    private:
        SpriteClipModel           _model;
        EditorThumbnailCache      _atlasThumbnail;
        float2                    _dragStartUV;     ///< 아틀라스 끌기를 시작한 uv
        optional<SpriteFrameEdge> _dragEdge;        ///< 끄는 가장자리. 비어 있으면 사각형 끌기거나 끌지 않는다
        uint8                     _bAtlasPress : 1; ///< 아틀라스 위에서 눌러 아직 떼지 않았다
        uint8                     _bEdgeMoved  : 1; ///< 이번 끌기에서 가장자리가 옮겨졌다(뗄 때 되돌리기 하나)
    };
} // namespace sw::editor
