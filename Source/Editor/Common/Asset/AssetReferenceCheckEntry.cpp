/**
 * @file AssetReferenceCheckEntry.cpp
 * @brief 에디터 모듈의 헤드리스 에셋 참조 검사 진입점(`checkEditorAssetReferences`)입니다. `App --check-asset-refs`(ctest `ToolCheckAssetRefs`)가 부릅니다.
 * @details 두 가지를 본다. 1) 리플렉션 `AssetPath` 프로퍼티가 모두 칸 목록(`Config/Dev/AssetReferenceFields.generated.json`)에 있는지 — 새 경로 칸이 목록을 비켜 가면
 *          그 칸의 끊긴 경로를 아무도 못 잡는다. 2) `Content/` 의 글 에셋과 테스트 CMake 의 경로 칸이 모두 있는 것을 가리키는지(임포트 산출물은 원본이 있으면 있다).
 *          커밋 전 검사(`Scripts/lint/AssetReferences.py`)와 같은 칸 목록 · 같은 규칙이다 — 그쪽은 스테이지만, 이쪽은 디스크의 트리(임포트 산출물 포함)를 본다.
 *          개발 도구라 에디터 모듈에 둔다(배포 엔진에는 검증 코드가 없다 — 언리얼 Data Validation 이 에디터 모듈인 것과 같다).
 * @note 모듈 전용입니다. `EditorTest` 는 이 파일을 넣지 않는다.
 */
#include "pch.h"

#include "Core/Container/pair.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorContentScan.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "ModuleAPI/ABI/EditorAPI.h"

namespace sw::editor
{
    namespace
    {
        struct AssetReferenceCheckEntryInternal
        {
            /** @brief 리플렉션 `AssetPath` 프로퍼티 가운데 칸 목록에 없는 것을 오류로 남깁니다. 반환은 그 수입니다. */
            static uint32 checkReflectedFields( const AssetReferenceFields& params )
            {
                uint32 problemCount{ 0 };
                editor::getSubsystem<TypeRegistry>().forEachType( [&params, &problemCount]( const TypeInfo& type )
                {
                    for ( const PropertyInfo& property : type._listProperty )
                    {
                        if ( property._metadata->_bAssetPath == SW_FALSE || params.isField( property._name.c_str() ) )
                            continue;
                        SW_LOG_ERROR_ID( "AssetReferenceCheck.FieldNotListed", "%#.%# is an AssetPath property but '%#' is not in %# - its paths would not be checked or fixed on move. The build rewrites that file: commit it",
                                         type._fullyQualifiedName.c_str(), property._name.c_str(), property._name.c_str(), AssetReferenceFields::kGeneratedFile );
                        ++problemCount;
                    }
                } );
                return problemCount;
            }

            /** @brief 파일 하나의 참조 가운데 없는 것을 `파일:줄` 과 함께 오류로 남깁니다. 반환은 그 수입니다. */
            static uint32 checkFile( const AssetReferenceFields& params, const AssetIDSet& assets, string_view label, string_view text, AssetReferenceSyntax syntax )
            {
                vector<AssetReference> listReference;
                params.extract( text, syntax, listReference );
                uint32 problemCount{ 0 };
                for ( const AssetReference& reference : listReference )
                {
                    if ( params.isChecked( reference._path ) == false || params.resolves( reference._path, assets ) )
                        continue;
                    SW_LOG_ERROR_ID( "AssetReferenceCheck.Missing", "%#:%#: %# = '%#' does not exist", label, reference._line, reference._field, reference._path );
                    ++problemCount;
                }
                return problemCount;
            }

            /** @brief 검사 전체입니다. 반환은 문제 수이고, 칸 목록 · 트리를 읽지 못하면 음수입니다. */
            static int32 checkAssetReferences()
            {
                const string&        projectRoot = ProcessRoots::get()._project.str();
                AssetReferenceFields params;
                if ( params.load( projectRoot ) == false )
                    return -1;
                uint32 problemCount = checkReflectedFields( params );

                // 트리의 파일이 있는 것이다(임포트 산출물 포함 — ctest 는 임포트 단계 뒤에 돈다). 참조자는 그 가운데 글 에셋이다.
                const string&  contentRoot = editor::getSubsystem<AssetPaths>().getRootFolderPath();
                vector<string> listFile;
                if ( EditorContentScan::collectFiles( contentRoot, contentRoot, "", listFile, true ) == false ) // 남의 시나리오 사본은 트리의 에셋이 아니다
                {
                    SW_LOG_ERROR_ID( "AssetReferenceCheck.ContentUnlisted", "Asset reference check: the files under %# could not be listed", contentRoot );
                    return -1;
                }
                AssetIDSet                   assets;
                vector<pair<string, string>> listReferrer; // (디스크 경로, 리소스 id)
                for ( const string& filePath : listFile )
                {
                    string assetID;
                    if ( FileUtil::makeRelativePath( contentRoot, filePath, assetID ) == false )
                        continue;
                    assetID = FileUtil::normalizeSeparators( assetID );
                    assets.addFile( assetID );
                    if ( params.isReferrer( assetID ) )
                        listReferrer.emplace_back( filePath, assetID );
                }

                uint32 scannedCount{ 0 };
                for ( const auto& [filePath, assetID] : listReferrer )
                {
                    string text;
                    if ( FileUtil::readTextFile( filePath, text ) == false )
                        return -1; // 이유는 FileUtil 이 남겼다
                    problemCount += checkFile( params, assets, "Content/" + assetID, text, AssetReferenceFields::syntaxOf( assetID ) );
                    ++scannedCount;
                }
                for ( const string& cmakeFile : params.getCMakeFiles() )
                {
                    string text;
                    if ( FileUtil::readTextFile( FileUtil::joinPath( projectRoot, cmakeFile ), text ) == false )
                        return -1; // 칸 목록이 적은 파일이 없으면 목록이 틀린 것이다
                    problemCount += checkFile( params, assets, cmakeFile, text, AssetReferenceSyntax::CMake );
                    ++scannedCount;
                }
                SW_LOG_INFO_ID( "AssetReferenceCheck.Summary", "Asset reference check: %# files, %# problems (fields: %#)", scannedCount, problemCount, AssetReferenceFields::kGeneratedFile );
                return static_cast<int32>( problemCount );
            }
        };
    } // namespace
} // namespace sw::editor

extern "C" SW_MODULE_API int32 checkEditorAssetReferences( const sw::ISubsystemLocator* pService )
{
    // 인스턴스가 없는 헤드리스 호출이라 서비스 범위를 이 호출 동안만 묶는다(`importEditorAssets` 와 같은 길).
    if ( pService != nullptr )
        sw::editor::bindEditorService( *pService );
    const int32 result = sw::editor::AssetReferenceCheckEntryInternal::checkAssetReferences();
    if ( pService != nullptr )
        sw::editor::unbindEditorService();
    return result;
}
