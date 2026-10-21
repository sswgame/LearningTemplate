/**
 * @file EditorPanelVisibility.h
 * @brief 에디터 패널마다 열림 · 닫힘을 기억하는 사용자 상태(`Saved/Editor/windows.json`, 이름 붙인 레이아웃의 `<이름>.windows.json`)입니다.
 * @details 사용자 상태는 리플렉션 JSON 이다(계획 StructureSimplification TX2 — 예전 `key=value` 의 `windows.ini`). `Saved/Editor/` 는 git 이 무시하는
 *          앱 상태라 옛 파일을 읽는 길을 두지 않는다 — 옛 `windows.ini` 가 남아 있어도 읽지 않고, 다음 저장이 새 파일을 쓴다.
 *          적지 않은 패널은 지금 상태 그대로 둔다(새로 생긴 패널은 등록한 기본 가시성).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /**
     * @struct EditorPanelVisibility
     * @brief 패널 id → 열림 표입니다. 파일 머리말 참고.
     */
    REFLECT()
    struct EditorPanelVisibility
    {
        REFLECT_BODY();

        PROPERTY()
        map<string, bool> _mapOpen; ///< 패널 id(`EditorPanelEntry::_id`) → 열려 있는가

        /** @brief @p absPath 에 JSON 으로 씁니다(원자적). */
        [[nodiscard]] bool saveFile( string_view absPath ) const;
        /** @brief @p absPath 에서 엄격하게 읽습니다. 없으면 `Missing`(로그 없음), 틀리면 `Invalid`(키와 함께 오류를 남겼다)입니다. */
        [[nodiscard]] ConfigReadResult loadFile( string_view absPath );
        /** @brief @p panelID 를 적었으면 @p outOpen 에 담고 true 입니다. 적지 않았으면 false 이고 @p outOpen 은 그대로입니다. */
        bool findOpen( string_view panelID, bool& outOpen ) const;
    };
} // namespace sw::editor
