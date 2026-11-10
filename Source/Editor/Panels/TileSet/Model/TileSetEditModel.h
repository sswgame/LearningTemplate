/**
 * @file TileSetEditModel.h
 * @brief 타일셋 편집 창의 모델 — 런타임 내용(`TileSetContent`) 한 벌, 고른 브러시 · 규칙, 브러시 · 규칙 · 이웃 패턴 · 모습(셀 · 프레임) 편집, 규칙 위반 목록, 상태 줄. ImGui 를 모릅니다.
 * @details 런타임이 읽는 `.tileset.xml` 을 그대로 고친다(새 형식 · 별도 사본 없음). 읽기 · 쓰기 · 검증은 엔진의 짝(`TileSetAsset::loadFromXMLText` · `TileSetContent::saveToXMLText` ·
 *          `collectIssues`)이라 에디터가 보이는 문제와 로드 오류가 같다. 유니티 Tile Palette · Rule Tile 인스펙터, Godot TileSet 편집기의 아틀라스 셀 고르기와 같은 일을 한다.
 *          패널(`TileSetPanel`)은 그리기와 dirty · 되돌리기 기록만 한다(스프라이트 클립 창과 같은 선). 편집 명령은 바뀌었으면 true 를 돌려준다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/TileMap/TileSetAsset.h"

namespace sw::editor
{
    /**
     * @class TileSetEditModel
     * @brief 타일셋 편집 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class TileSetEditModel
    {
    public:
        TileSetEditModel();

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기 글
        // ------------------------------------------------------------------------------
        /** @brief @p path 를 읽습니다. 파일이 없으면 빈 타일셋(1 × 1 아틀라스)으로 시작합니다. 결과는 상태 줄에도 남습니다. */
        ToolAssetLoadResult loadDocument( string_view path );
        /** @brief @p path 에 씁니다. 규칙 위반이 있으면 쓰지 않고 상태 줄에 첫 문제를 남기며 false 입니다(런타임이 읽지 못할 파일을 쓰지 않는다). */
        [[nodiscard]] bool saveDocument( string_view path );
        /** @brief 되돌리기 기록용 글입니다(파일과 같은 글). */
        string captureText() const { return _content.saveToXMLText(); }
        /** @brief 되돌리기 기록 글로 돌립니다. 규칙 위반이 든 글도 받는다(편집 중의 상태) — 읽기 규칙을 건너뛰지 않으려 글을 형식만 읽고 위반은 목록으로 보인다. */
        void applyText( string_view text );

        const TileSetContent& getContent() const { return _content; }
        const string&         getStatus() const { return _status; }
        /** @brief 지금 내용의 규칙 위반입니다(먼저 비운다). */
        void collectIssues( vector<TileSetIssue>& outListIssue ) const;

        // ------------------------------------------------------------------------------
        // 2) 아틀라스
        // ------------------------------------------------------------------------------
        [[nodiscard]] bool setAtlasPath( string_view path );
        /** @brief 아틀라스 격자(열 · 행, 1 이상)를 바꿉니다. 셀 번호는 그대로라 밖으로 나간 셀은 위반으로 보인다(조용히 옮기지 않는다). */
        [[nodiscard]] bool setAtlasGrid( int32 columnCount, int32 rowCount );
        [[nodiscard]] bool setTileSize( float32 tileSize );

        // ------------------------------------------------------------------------------
        // 3) 브러시
        // ------------------------------------------------------------------------------
        int32 getSelectedBrush() const { return _selectedBrush; }
        void  selectBrush( int32 brushIndex );
        /** @brief 고른 브러시입니다. 없으면 nullptr 입니다. */
        const TileBrush* findSelectedBrush() const;
        /** @brief 겹치지 않는 이름(`tile`, `tile2` …)의 브러시를 끝에 더하고 고릅니다. @p bRuleTile 이면 규칙 하나(모든 이웃 상관없음)로 시작합니다. 모습은 셀 0 입니다. */
        void               addBrush( bool bRuleTile );
        [[nodiscard]] bool removeSelectedBrush();
        /** @brief 고른 브러시의 이름을 바꿉니다. 비었거나 다른 브러시와 겹치면 상태 줄에 알리고 false 입니다(맵은 브러시를 이름으로 칠한다). */
        [[nodiscard]] bool renameSelectedBrush( string_view name );
        [[nodiscard]] bool setSelectedSolid( bool bSolid );
        /** @brief 이동 비용(1 ~ 255, 255 는 막힘)으로 묶어 씁니다. */
        [[nodiscard]] bool setSelectedNavCost( int32 navCost );
        [[nodiscard]] bool setSelectedOutsideSame( bool bOutsideIsSame );

        // ------------------------------------------------------------------------------
        // 4) 규칙(규칙 타일)
        // ------------------------------------------------------------------------------
        /** @brief 고른 규칙 번호입니다. 비어 있으면 브러시의 기본 모습을 고친다. */
        optional<int32> getSelectedRule() const { return _selectedRule; }
        /** @brief 규칙을 고릅니다. 비어 있으면 브러시의 기본 모습입니다. */
        void selectRule( optional<int32> ruleIndex );
        /** @brief 고른 브러시 끝에 규칙(모든 이웃 상관없음, 모습은 기본 모습)을 더하고 고릅니다. 브러시가 없으면 false 입니다. */
        [[nodiscard]] bool addRule();
        /** @brief 고른 규칙을 지웁니다. 규칙이 하나뿐이면 지우지 않고 false 입니다(규칙 없는 규칙 타일은 로드 오류다 — 그냥 타일로 바꾸려면 브러시를 다시 만든다). */
        [[nodiscard]] bool removeSelectedRule();
        /** @brief 고른 규칙을 @p delta 칸 옮깁니다(앞 규칙이 먼저 맞는다). 끝을 넘으면 false 입니다. */
        [[nodiscard]] bool moveSelectedRule( int32 delta );
        /** @brief 고른 규칙의 이웃 @p neighbor(0 ~ 7, `TileSetAsset::kNeighborOffset` 순서) 조건을 상관없음 → 같음 → 다름 → 상관없음으로 돌립니다. */
        [[nodiscard]] bool cycleNeighbor( int32 neighbor );

        // ------------------------------------------------------------------------------
        // 5) 모습(고른 규칙, 없으면 기본 모습)
        // ------------------------------------------------------------------------------
        /** @brief 고친 대상인 모습입니다. 없으면 nullptr 입니다. */
        const TileVisual* findSelectedVisual() const;
        /** @brief 아틀라스 셀 @p cell 을 모습으로 둡니다. @p bAppendFrame 이면 프레임으로 덧붙입니다(애니메이션 — fps 가 0 이면 기본 4 fps 로 둔다). 범위 밖이면 false 입니다. */
        [[nodiscard]] bool pickCell( int32 cell, bool bAppendFrame );
        /** @brief 마지막 프레임을 뺍니다. 하나만 남았으면 false 입니다(모습 없는 브러시는 로드 오류다). */
        [[nodiscard]] bool removeLastFrame();
        [[nodiscard]] bool setFramesPerSecond( float32 framesPerSecond );

    private:
        TileBrush*  editSelectedBrush();
        TileVisual* editSelectedVisual();
        /** @brief 고른 번호를 지금 목록 안으로 묶습니다. */
        void clampSelection();

    private:
        TileSetContent  _content;
        string          _status;
        int32           _selectedBrush;
        optional<int32> _selectedRule;
    };
} // namespace sw::editor
