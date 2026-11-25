/**
 * @file EditorDefaultAssets.h
 * @brief 에디터 기본 에셋 표(`editor/data/editordefaultassets.xml`)입니다 — 에디터가 이름으로 여는 데이터 파일의 경로입니다.
 * @details 엔진의 `EngineDefaultAssets` 와 같은 꼴이다(모든 칸이 필수, 값의 정본은 데이터). 셸(`ImGuiEditor`)이 기동에 읽어 들고 에디터 컨텍스트가 자기 범위에 빌려 건다
 *          (`editor::getSubsystem<const EditorDefaultAssets>()`). 에디터 컨텍스트 없이 도는 명령줄 도구는 `load` 로 읽는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw::editor
{
    /** @struct EditorDefaultAssets @brief 파일 머리말 참고. */
    REFLECT()
    struct EditorDefaultAssets
    {
        REFLECT_BODY();

        /** @brief 표 파일입니다. 다른 경로는 모두 이 표에 있다. */
        static constexpr const utf8* kAsset = "editor/data/editordefaultassets.xml";

        PROPERTY( Required )
        DataPath _localizationProject; ///< 에디터 UI 문구의 로컬라이제이션 프로젝트 — 엔진 테이블과 따로 있어 배포본에 실리지 않는다
        PROPERTY( Required )
        DataPath _iconGlyphTable; ///< 아이콘 글꼴의 이름 → 코드포인트 표(`EditorIconGlyphTable`)
        PROPERTY( Required )
        DataPath _componentIconTable; ///< 컴포넌트 아이콘 표(`EditorComponentIconTable` — Category → 아이콘 · 색 이름)
        PROPERTY( Required )
        DataPath _panelNames; ///< 패널 · 팝업 창의 보이는 이름 표(`EditorPanelTextTable`)
        PROPERTY( Required )
        DataPath _contentBrowserFavorites; ///< 콘텐츠 브라우저의 즐겨찾기 폴더 표(`ContentBrowserFavoriteTable`)

        /** @brief 표를 읽고 칸마다 파일이 있는지 봅니다(`DefaultAssetTable::load`). 실패하면 이유를 남기고 false 입니다 — 대신 쓸 값은 없다. */
        [[nodiscard]] static bool load( EditorDefaultAssets& outAssets );
    };
} // namespace sw::editor
