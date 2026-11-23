/**
 * @file CatalogMapCheck.cpp
 * @brief 맵 검사 "카탈로그 파일" — 활성 팩의 데이터 카탈로그를 그 종류의 로더로 읽어, 읽지 못하거나 로더가 경고한 파일을 결과로 적습니다.
 * @details 카탈로그 창의 Validate 와 같은 판정이다(`CatalogValidation`). 창은 연 파일 하나를, 맵 검사는 팩 전체를 본다 — 씬이 쓰는 카탈로그가 깨진 것을 플레이 전에 안다.
 */
#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorMapCheck.h"
#include "Editor/Panels/Catalog/Model/CatalogValidation.h"

#include "Engine/Content/AssetTypeRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct CatalogMapCheckInternal
        {
            static constexpr int32 kOrder = 100;

            static void validateCatalogs( const GameObjectManager& /*manager*/, EditorMapCheckReport& outReport )
            {
                vector<const AssetTypeDef*> listKind;
                vector<string>              listWarning;
                for ( const string& assetID : outReport.getPackAssets() )
                {
                    AssetTypeRegistry::collectPathMatches( assetID, listKind );
                    if ( listKind.size() != 1 )
                        continue; // 카탈로그가 아니거나(0) 종류가 겹치는(스키마 테스트가 잡는다) 파일 — 카탈로그 창의 목록과 같은 규칙
                    const bool bLoaded = CatalogValidation::validate( *listKind[0], assetID, listWarning );
                    for ( const string& warning : listWarning )
                    {
                        outReport.add( bLoaded ? ValidationSeverity::Warning : ValidationSeverity::Error, assetID, warning );
                    }
                    if ( bLoaded == false && listWarning.empty() )
                        outReport.add( ValidationSeverity::Error, assetID, "the catalog could not be loaded" );
                }
            }
        };
    } // namespace

    SW_EDITOR_MAP_CHECK( CatalogFiles, "catalog.files", CatalogMapCheckInternal::kOrder, &CatalogMapCheckInternal::validateCatalogs );
} // namespace sw::editor
