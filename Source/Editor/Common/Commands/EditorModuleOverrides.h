/**
 * @file EditorModuleOverrides.h
 * @brief 모듈 켜고 끄기의 ImGui 없는 반쪽입니다 — 프로젝트 매니페스트의 `_listModuleOverride` 고쳐 쓰기와 바꿨을 때의 미리보기.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct ModuleInactiveEntry;
    struct ModuleResolveContext;

    class ModuleCatalog;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorModuleOverrideUtil
     * @brief 언리얼 Plugins 창의 켜고 끄기에 해당합니다. CMake 가 같은 매니페스트로 짓기 때문에 실행 중에 켜지 않고, 매니페스트를 고친 뒤 빌드하고 다시 시작합니다.
     */
    struct SW_EDITOR_API EditorModuleOverrideUtil
    {
        /**
         * @brief 프로젝트 매니페스트 글 @p manifestJSON 의 `_listModuleOverride` 에서 @p moduleName 을 @p bEnabled 로 둔 새 글을 씁니다.
         * @details 그 모듈의 기본(@p bEnabledByDefault)과 같아지면 줄을 지웁니다 — 덮어쓰기는 기본과 다를 때만 남깁니다. 들여쓰기는 4 칸입니다. 형식이 틀리면 false 입니다.
         */
        [[nodiscard]] static bool setOverride( string_view manifestJSON, string_view moduleName, bool bEnabled, bool bEnabledByDefault, string& outManifestJSON );
        /**
         * @brief @p catalog 사본의 프로젝트 표에 @p moduleName = @p bEnabled 를 더해 다시 풀고, 켜짐이 바뀌는 모듈(그 모듈과 의존 때문에 함께 바뀌는 것)을 모읍니다.
         * @return 풀기에 실패하면(순환 · 없는 의존) false 이고 @p outError 에 이유가 있습니다.
         */
        [[nodiscard]] static bool previewToggle( const ModuleCatalog& catalog, const ModuleResolveContext& context, string_view moduleName, bool bEnabled,
                                                 vector<ModuleInactiveEntry>& outListNewlyInactive, vector<string>& outListNewlyActive, string& outError );
    };
} // namespace sw::editor
