#include "pch.h"

#include "App/EditorModuleHeadlessTasks.h"

#include "App/ClientModuleManager.h"

#include "Core/Foundation/DynamicModules.h"
#include "Core/Log/Logger.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "ModuleManager/ModuleInstanceUtil.h"

#include "sw/config/ConfigConstants.h"

// 동적 모듈 구성에만 있다 — 정적 구성(배포 · 웹 · Android 개발)은 에디터 모듈이 없고 모듈 라이브러리를 올리고 내리는 길(`ModuleLibraryUtil::unloadModuleLibrary`)도 짓지 않는다.
// 부르는 자리(`AppHeadlessTasks.cpp`)는 `build::kStaticModules` 로 가른다 — 버려지는 갈래라 정의가 없어도 된다(docs/agents/BuildConfigSwitch.md 첫 줄).
#if SW_WITH_DYNAMIC_MODULES
    #include "Core/File/FileUtil.h"
    #include "Core/Module/ModuleLibraryUtil.h"
    #include "Core/Path/AbsolutePath.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다. 인스턴스 없이 에디터 모듈을 올려 부르는 헤드리스 경로입니다. */
        struct EditorModuleHeadlessTasksInternal
        {
            /**
             * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 진입점 @p pSymbol 하나를 부르고 내립니다(헤드리스 임포트 · 로컬라이제이션 공통).
             * @details 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 걷습니다. @p call 은 `( TEntryFn, const ISubsystemLocator& ) -> bool` 이고 엔진 서비스 범위를 받습니다.
             * @param symbols 에디터 모듈의 ABI 심볼 이름(`ClientModuleManager::getModuleSymbols`)입니다.
             * @param pPurpose 로그에 쓰는 작업 이름입니다(`"Asset importing"`).
             */
            template <typename TEntryFn, typename TCall>
            [[nodiscard]] static bool callEditorModuleEntry( const ModuleAPISymbols& symbols, const utf8* pSymbol, const utf8* pPurpose, TCall&& call )
            {
                const string                 modulePath  = ModuleLibraryUtil::findModuleLibraryPath( sw::config::kTargetEditorModule );
                const optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( modulePath );
                const ModuleLibraryHandle    library{ libraryPath.has_value() && FileUtil::exists( modulePath ) ? ModuleLibraryUtil::loadLibraryAt( *libraryPath ) : nullptr };
                if ( library.isValid() == false )
                {
                    SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.NeedsEditorModuleBin", "%# needs the editor module in Bin/Modules: %#", pPurpose, modulePath.c_str() );
                    return false;
                }

                // 로드하는 순간 모듈의 정적 등록기가 전역 헤드에 매달린다. 모듈 이름으로 등록해 두어야 언로드하기 전에 해제할 수 있다.
                bool bSucceeded = false;
                if ( engine::registerModuleTypes( sw::config::kTargetEditorModule ) == false )
                    SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.CannotRegisterEditorModule", "%# cannot register the editor module types", pPurpose );
                else if ( ModuleInstanceUtil::matchesModuleABI( library, symbols ) )
                {
                    const TEntryFn pfnEntry = reinterpret_cast<TEntryFn>( library.findSymbol( pSymbol ) );
                    if ( pfnEntry == nullptr )
                        SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.EditorModuleDoesNot", "The editor module does not export %#", pSymbol );
                    else
                    {
                        SubsystemScope service;
                        engine::bindModuleServices( service, false );
                        bSucceeded = call( pfnEntry, static_cast<const ISubsystemLocator&>( service ) );
                    }
                }

                engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
                // 언로드하지 못하면(다른 코드가 아직 그 이미지의 이벤트 채널을 구독한다) 프로세스 끝까지 로드된 채로 남을 뿐이다. 이유는 경고로 남는다.
                (void)ModuleLibraryUtil::unloadModuleLibrary( sw::config::kTargetEditorModule, library.getOSHandle() );
                return bSucceeded;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly )
    {
        // 인스턴스 없이 부르므로 엔진 서비스 범위를 직접 넘긴다. 임포터가 작업 시스템으로 압축을 나눈다.
        return EditorModuleHeadlessTasksInternal::callEditorModuleEntry<PFN_ImportEditorAssets>(
            ClientModuleManager::getModuleSymbols(), kImportEditorAssetsSymbol, "Asset importing", [kind, bCheckOnly]( PFN_ImportEditorAssets pfnImport, const ISubsystemLocator& service )
        {
            return pfnImport( static_cast<uint32>( kind ), bCheckOnly ? 1u : 0u, &service ) == 0;
        } );
    }

    bool runLocalizationWithEditorModule( EditorLocalizationTask task, string_view poPath, string_view projectArgument )
    {
        const string poPathText( poPath );
        const string projectText( projectArgument );
        return EditorModuleHeadlessTasksInternal::callEditorModuleEntry<PFN_RunEditorLocalizationTask>(
            ClientModuleManager::getModuleSymbols(), kRunEditorLocalizationTaskSymbol, "Localization tools",
            [task, &poPathText, &projectText]( PFN_RunEditorLocalizationTask pfnRun, const ISubsystemLocator& service )
        {
            return pfnRun( static_cast<uint32>( task ), poPathText.c_str(), projectText.c_str(), &service ) == 0;
        } );
    }

    bool validateAssetReferencesWithEditorModule()
    {
        return EditorModuleHeadlessTasksInternal::callEditorModuleEntry<PFN_CheckEditorAssetReferences>(
            ClientModuleManager::getModuleSymbols(), kCheckEditorAssetReferencesSymbol, "Asset reference check", []( PFN_CheckEditorAssetReferences pfnCheck, const ISubsystemLocator& service )
        {
            return pfnCheck( &service ) == 0;
        } );
    }
} // namespace sw
#endif // SW_WITH_DYNAMIC_MODULES
