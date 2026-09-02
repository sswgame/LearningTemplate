/**
 * @file AssetEditorManager.h
 * @brief 애셋 경로 → 도구 패널 오픈 디스패처 (오버라이드는 선택)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @class AssetEditorManager
     * @brief 레지스트리 매핑으로 패널을 열고, 플러그인 접미사 오버라이드만 보관합니다.
     */
    class AssetEditorManager
    {
    public:
        AssetEditorManager()  = default;
        ~AssetEditorManager() = default;

        void        registerAssetEditor( string_view extension, string_view windowTitle );
        string_view findEditorForExtension( string_view extension ) const;
        /** @brief 복합 확장자(.prefab.xml 등)를 포함해 가장 긴 접미사 매칭을 반환합니다. */
        string_view findEditorForPath( string_view assetPath ) const;
        bool        openAssetInEditor( string_view assetPath );
        void        registerDefaultMappings();

    private:
        map<string, string> _mapOverrideExtToTitle;
    };
} // namespace sw::editor
