#include "pch.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Material/Dev/MaterialGraph.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphCook.h"

namespace sw::editor
{
    namespace
    {
        struct MaterialGraphAssetTypeActionsInternal
        {
            /**
             * @brief 그래프가 바뀌면 생성물을 다시 맞춥니다. 쓴 `.gen.material` 은 다음 감시 사건이 캐시와 자식 머티리얼로 잇는다(연쇄 리로드). 임포트가 곧 리로드다.
             * @details 하위 그래프가 바뀌면 쓰는 쪽도 달라지므로 그래프 하나가 아니라 전부 본다. 실패는 노드 이름과 함께 오류로 남고 앞 생성물이 그대로 그려진다.
             */
            static bool importGraph( const AssetPaths& assetPaths, string_view /*relativePath*/ )
            {
                (void)MaterialGraphCook::refreshAll( assetPaths, assetPaths.getRootFolderPath(), nullptr );
                return true;
            }
        };

        /** @brief 새 그래프의 틀입니다 — 출력 노드에 색 파라미터 하나가 이어진 가장 작은 그래프입니다. */
        constexpr const utf8* kNewGraphText = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialGraphDef formatVersion="0">
	<_listNode>
		<MaterialGraphNodeDef _name="BaseColor" _kind="ColorParameter" _position="0,0">
			<_listValue>
				<MaterialGraphValue _name="Parameter" _value="_baseColor" />
			</_listValue>
		</MaterialGraphNodeDef>
		<MaterialGraphNodeDef _name="Out" _kind="Output" _position="280,0">
			<_listLink>
				<MaterialGraphLink _pin="BaseColor" _fromNode="BaseColor" _fromPin="RGB" />
			</_listLink>
		</MaterialGraphNodeDef>
	</_listNode>
</MaterialGraphDef>
)";

        constexpr EditorNewAssetTemplate kMaterialGraphTemplate{ "matgraph", "Material Graph", "new_material", ".matgraph.xml", nullptr, kNewGraphText, nullptr };

        const EditorAssetMatchRule kArrMaterialGraphRule[] = {
            EditorAssetMatchRule::engineType( MaterialGraphDef::kAssetType, { nullptr, &MaterialGraphAssetTypeActionsInternal::importGraph, AssetImportThread::Editor, AssetReferenceUse::ResolvedAtUse } ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( MaterialGraph, MaterialGraphDef::kAssetType._name.data(), 505, "Material Graph", "Material Graphs", "Material Graph", SW_EDITOR_ICON_NAME( "Material" ), SW_THEME_COLOR_KEY( "Asset", "Material" ), true, false,
                          kArrMaterialGraphRule, SW_COUNT_OF( kArrMaterialGraphRule ), &kMaterialGraphTemplate, nullptr );
} // namespace sw::editor
