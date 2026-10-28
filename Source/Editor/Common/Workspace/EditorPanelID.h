/**
 * @file EditorPanelID.h
 * @brief 패널 창구(`IEditorPanelService`)가 받는 패널 id 형식과, 패널 층 밖(커맨드 · 뷰포트)이 부르는 코어 패널의 id 입니다.
 * @details 패널 id 를 글자로 받으면 오타가 실행 중 false 로만 드러난다. 창구는 이 형식만 받으므로 글자를 직접 넘기는 호출은 빌드가 막는다.
 *          패널 클래스의 `kPanelID` 도 여기 상수에서 온다(`SceneViewPanel::kPanelID = editorpanel::kSceneView.c_str()`) — 등록 · 조회 · 창구가 한 상수를 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorPanelID
     * @brief 패널 id 하나입니다. 글자에서 암시적으로 만들어지지 않습니다 — 상수(`editorpanel::kSceneView`)로만 만듭니다.
     */
    struct EditorPanelID
    {
        /** @brief 상수 정의에서만 씁니다(`editorpanel::kSceneView`). */
        explicit constexpr EditorPanelID( const utf8* pValue )
            : _pValue{ pValue }
        {
        }

        constexpr const utf8* c_str() const { return _pValue; }
        string_view           getView() const { return string_view{ _pValue }; }

    private:
        const utf8* _pValue;
    };

    /** @brief 패널 층 밖에서 부르는 코어 패널 id 입니다. 패널 클래스의 `kPanelID` 가 이 값을 씁니다. */
    namespace editorpanel
    {
        inline constexpr EditorPanelID kSceneView{ "scene_view" };
        inline constexpr EditorPanelID kGameView{ "game_view" };
    } // namespace editorpanel
} // namespace sw::editor
