/**
 * @file HousingMapCheck.cpp
 * @brief 맵 검사 "하우징 부품 프리팹" — 활성 팩의 부품 카탈로그(housingparts)마다 부품이 가리키는 프리팹 파일이 있는지 봅니다.
 * @details 부지 하나의 검사(크기 · 겹침 · 같은 id)는 `HousingPlotComponent::validatePlot` 이 한다 — 여기는 씬에 부지가 없어도 걸려야 하는 카탈로그 쪽만 본다.
 *          없는 프리팹은 플레이어가 그 부품을 놓을 때 처음 드러나 저장 뒤에야 알게 된다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Scene/EditorMapCheck.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetTypeRegistry.h"

#include "GameFramework/Kits/Feature/World/Housing/Catalog/HousingPart.h"

namespace sw::editor
{
    namespace
    {
        struct HousingMapCheckInternal
        {
            static constexpr int32 kOrder = 900;

            static void validatePartPrefabs( const GameObjectManager& /*manager*/, EditorMapCheckReport& outReport )
            {
                const AssetPaths&           paths = editor::getSubsystem<AssetPaths>();
                vector<const AssetTypeDef*> listKind;
                for ( const string& assetID : outReport.getPackAssets() )
                {
                    AssetTypeRegistry::collectPathMatches( assetID, listKind );
                    if ( listKind.size() != 1 || string_view{ listKind[0]->_name } != string_view{ HousingPartCatalog::kAssetType._name } )
                        continue;
                    const shared_ptr<const HousingPartCatalog> pCatalog = SharedAssetCache<HousingPartCatalog>::acquire( assetID ).snapshot();
                    if ( pCatalog == nullptr )
                        continue; // 읽지 못하는 카탈로그는 카탈로그 검사가 알린다
                    for ( const HousingPartDef& part : pCatalog->getParts() )
                    {
                        if ( part._prefabPath.empty() )
                            continue; // 필수 칸이 비면 카탈로그 읽기가 거절한다
                        const optional<DiskPath> prefab = paths.toDiskPath( part._prefabPath.path() );
                        if ( prefab.has_value() && FileUtil::isRegularFile( *prefab ) )
                            continue;
                        StringBuilder<constant::buffer::kMax256> message;
                        message.appendFormat( "part '%#' points at a prefab that does not exist: '%#'", part._id.c_str(), part._prefabPath.path() );
                        outReport.add( ValidationSeverity::Error, assetID, message.view() );
                    }
                }
            }
        };
    } // namespace

    SW_EDITOR_MAP_CHECK( HousingPartPrefabs, "housing.partPrefabs", HousingMapCheckInternal::kOrder, &HousingMapCheckInternal::validatePartPrefabs );
} // namespace sw::editor
