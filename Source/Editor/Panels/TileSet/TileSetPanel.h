/**
 * @file TileSetPanel.h
 * @brief 타일셋 편집 창(`.tileset.xml`)입니다 — 아틀라스 격자에서 셀을 골라 브러시 · 애니메이션 프레임 · 규칙 타일의 모습을 정하고, 이웃 패턴을 3 × 3 단추로 돌리며, solid · 이동 비용 · 맵 밖 처리를 고친다.
 * @details 유니티 Tile Palette + Rule Tile 인스펙터, Godot TileSet 편집기(아틀라스 · terrain · 애니메이션)에 해당한다. 문서는 런타임이 읽는 파일 그대로이고(`TileSetContent`),
 *          상태와 명령은 `TileSetEditModel`(ImGui 없음)이 든다. 규칙 위반은 엔진의 판정(`TileSetContent::collectIssues`)을 그 브러시 · 규칙 옆에 보이고, 위반이 있으면 저장하지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Panels/TileSet/Model/TileSetEditModel.h"

namespace sw::editor
{
    /**
     * @class TileSetPanel
     * @brief 타일셋 편집 창입니다. 파일 머리말 참고.
     */
    class TileSetPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "tileset_editor";

        TileSetPanel();

        const utf8*        getPanelTitle() const override { return "Tile Set Editor"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        void               shutdown() override;

        bool                    isTileSetDocumentLoaded() const { return isDocumentLoaded(); }
        const TileSetEditModel& getModel() const { return _model; }
        /** @brief 지난 그리기의 규칙 위반 수입니다(감시 값 `Editor.TileSetIssueCount`). */
        uint32 getIssueCount() const { return static_cast<uint32>( _listIssue.size() ); }

    private:
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override { return _model.captureText(); }
        void                applyDocumentText( string_view text ) override { _model.applyText( text ); }

        /** @brief 저장 · 다시 읽기 단추와 아틀라스 경로 · 격자 · 타일 크기 칸입니다. */
        void drawAtlasRows();
        /** @brief 브러시 목록(위반이 있는 브러시는 빨간 표시 하나)과 더하기 · 지우기 단추입니다. */
        void drawBrushList();
        /** @brief 고른 브러시의 이름 · solid · 이동 비용 · 맵 밖 칸입니다. */
        void drawBrushDetail( const TileBrush& brush );
        /** @brief 고른 브러시의 기본 모습과 규칙 목록, 고른 규칙의 이웃 패턴(3 × 3) · 결과 셀입니다. */
        void drawRuleSection( const TileBrush& brush );
        /** @brief 고친 대상인 모습의 프레임 · fps 줄입니다. */
        void drawVisualRow( const TileVisual& visual );
        /** @brief 아틀라스 격자입니다. 누른 셀이 고른 모습이 되고 Shift 는 프레임으로 덧붙입니다. 고른 모습의 셀은 강조색 틀입니다. */
        void drawAtlasGrid();
        /** @brief 셀 하나의 아틀라스 그림입니다(@p side 픽셀 정사각형). 아틀라스가 없으면 셀 번호 글입니다. */
        void drawCellImage( int32 cell, float32 side );
        /** @brief 고른 브러시 · 규칙의 위반을 그 자리에서 보입니다. @p ruleIndex 가 `kDefaultVisual` 이면 브러시 자체의 위반입니다. */
        void drawIssuesFor( optional<int32> brushIndex, optional<int32> ruleIndex );

    private:
        TileSetEditModel     _model;
        EditorThumbnailCache _atlasThumbnail;
        vector<TileSetIssue> _listIssue;  ///< 그리기마다 다시 모은 규칙 위반
        string               _nameBuffer; ///< 고른 브러시 이름 칸(칸을 떠날 때 옮긴다)
        int32                _nameBrush;  ///< `_nameBuffer` 를 채운 브러시 번호 — 고른 것이 바뀌면 다시 채운다
    };
} // namespace sw::editor
