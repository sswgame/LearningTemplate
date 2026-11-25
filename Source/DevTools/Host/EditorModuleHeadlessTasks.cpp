#include "pch.h"

#include "DevTools/Host/EditorModuleHeadlessTasks.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Log/Logger.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Content/ContentHash.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/HeadlessTask.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "ModuleManager/ModuleInstanceUtil.h"

#include "sw/config/ConfigConstants.h"

// 동적 모듈 구성에만 있다 — 정적 구성(배포 · 웹 · Android 개발)은 에디터 모듈이 없고 모듈 라이브러리를 올리고 내리는 길(`ModuleLibraryUtil::unloadModuleLibrary`)도 짓지 않는다.
// 부르는 자리(아래 작업 표의 본문)는 `build::kStaticModules` 로 가른다 — 버려지는 갈래라 정의가 없어도 된다(docs/agents/CppStyle.md "Build configuration switches" 첫 줄).
#if SW_WITH_DYNAMIC_MODULES
    #include "Core/File/FileUtil.h"
    #include "Core/Module/ModuleLibraryUtil.h"
    #include "Core/Path/AbsolutePath.h"

    #include "DevTools/Diagnostics/UnitTestRun.h"

    #include "Engine/Module/ModuleCatalog.h"
    #include "Engine/Module/ModuleRegistrationHost.h"

    #include "ModuleAPI/Service/IModuleStateStore.h"

    #include "ModuleManager/ModuleCatalogLoader.h"

namespace sw
{
    EditorModuleHeadlessSession::EditorModuleHeadlessSession()
        : _listExtension{}
        , _pLibrary{ nullptr }
        , _loadCount{ 0 }
        , _entryCallCount{ 0 }
        , _bLoadAttempted{ SW_FALSE }
        , _bTypesRegistered{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorModuleHeadlessSession::~EditorModuleHeadlessSession()
    {
        for ( auto it = _listExtension.rbegin(); it != _listExtension.rend(); ++it )
        {
            engine::unregisterModuleTypes( it->_name );
            (void)ModuleLibraryUtil::unloadModuleLibrary( it->_name, it->_pLibrary ); // 남으면 프로세스 끝까지 로드된 채다(이유는 경고로 남는다)
        }
        if ( _bTypesRegistered == SW_TRUE )
            engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
        if ( _pLibrary != nullptr )
        {
            SW_LOG_INFO_ID( "EditorModuleHeadlessTasks.SessionUnloaded", "Editor module loaded %# time(s) for %# headless task(s)", _loadCount, _entryCallCount );
            // 언로드하지 못하면(다른 코드가 아직 그 이미지의 이벤트 채널을 구독한다) 프로세스 끝까지 로드된 채로 남을 뿐이다. 이유는 경고로 남는다.
            (void)ModuleLibraryUtil::unloadModuleLibrary( sw::config::kTargetEditorModule, _pLibrary );
        }
    }

    void* EditorModuleHeadlessSession::findEntry( const utf8* pSymbol, const utf8* pPurpose )
    {
        if ( _bLoadAttempted == SW_FALSE )
        {
            _bLoadAttempted                      = SW_TRUE;
            const AbsolutePath        modulePath = ModuleLibraryUtil::findModuleLibraryPath( sw::config::kTargetEditorModule );
            const ModuleLibraryHandle library{ FileUtil::exists( modulePath ) ? ModuleLibraryUtil::loadLibraryAt( modulePath ) : nullptr };
            if ( library.isValid() == false )
            {
                SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.NeedsEditorModuleBin", "%# needs the editor module in Bin/Modules: %#", pPurpose, modulePath.c_str() );
                return nullptr;
            }
            _pLibrary = library.getOSHandle();
            ++_loadCount;
            // 로드하는 순간 모듈의 정적 등록기가 전역 헤드에 매달린다. 모듈 이름으로 등록해 두어야 언로드하기 전에 해제할 수 있다.
            if ( engine::registerModuleTypes( sw::config::kTargetEditorModule ) == false )
            {
                SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.CannotRegisterEditorModule", "%# cannot register the editor module types", pPurpose );
                return nullptr;
            }
            _bTypesRegistered = SW_TRUE;
            if ( ModuleInstanceUtil::matchesModuleABI( library, kEditorModuleSymbols ) == false )
            {
                engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
                _bTypesRegistered = SW_FALSE;
                return nullptr; // 대조가 이유를 남겼다
            }
        }
        if ( _pLibrary == nullptr || _bTypesRegistered == SW_FALSE )
            return nullptr;
        void* const pEntry = ModuleLibraryHandle{ _pLibrary }.findSymbol( pSymbol );
        if ( pEntry == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.EditorModuleDoesNot", "The editor module does not export %#", pSymbol );
            return nullptr;
        }
        ++_entryCallCount;
        return pEntry;
    }

    bool EditorModuleHeadlessSession::loadExtensions()
    {
        ModuleCatalog    catalog;
        ModuleResolution resolution;
        if ( ModuleCatalogLoader::loadAndResolve( ModuleCatalog::getBuildTargetMask(), catalog, resolution ) == false )
            return false;
        for ( const string& moduleName : resolution._listLoadOrder )
        {
            const ModuleManifest* const pManifest = catalog.findManifest( moduleName );
            if ( pManifest == nullptr || pManifest->_kind != ModuleKind::EditorExtension )
                continue;
            void* const pLibrary = ModuleLibraryUtil::loadLibraryAt( ModuleLibraryUtil::findModuleLibraryPath( moduleName ) );
            if ( pLibrary == nullptr )
            {
                SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.CannotLoadExtension", "Editor extension module %# could not be loaded", moduleName );
                return false;
            }
            _listExtension.push_back( ExtensionLibrary{ moduleName, pLibrary } );
            if ( engine::registerModuleTypes( moduleName ) == false )
                return false; // 등록이 이유를 남겼다
        }
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다. 인스턴스 없이 에디터 모듈을 올려 부르는 헤드리스 경로입니다. */
        struct EditorModuleHeadlessTasksInternal
        {
            /**
             * @brief 세션이 올린 에디터 모듈의 진입점 @p pSymbol 하나를 엔진 서비스 범위와 함께 부릅니다(헤드리스 임포트 · 로컬라이제이션 · 참조 검사 공통).
             * @details @p call 은 `( TEntryFn, const ISubsystemLocator& ) -> bool` 입니다. 모듈을 올리고 내리는 일은 세션이 한 번 한다.
             */
            template <typename TEntryFn, typename TCall>
            [[nodiscard]] static bool callEditorModuleEntry( EditorModuleHeadlessSession& session, const utf8* pSymbol, const utf8* pPurpose, TCall&& call )
            {
                const TEntryFn pfnEntry = reinterpret_cast<TEntryFn>( session.findEntry( pSymbol, pPurpose ) );
                if ( pfnEntry == nullptr )
                    return false;
                SubsystemScope service;
                engine::bindModuleSubsystems( service, false );
                return call( pfnEntry, static_cast<const ISubsystemLocator&>( service ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool importAssetsWithEditorModule( EditorModuleHeadlessSession& session, EditorImportKind kind, bool bCheckOnly )
    {
        // 인스턴스 없이 부르므로 엔진 서비스 범위를 직접 넘긴다. 임포터가 작업 시스템으로 압축을 나눈다.
        return EditorModuleHeadlessTasksInternal::callEditorModuleEntry<PFN_ImportEditorAssets>(
            session, kImportEditorAssetsSymbol, "Asset importing", [kind, bCheckOnly]( PFN_ImportEditorAssets pfnImport, const ISubsystemLocator& service )
        {
            return pfnImport( static_cast<uint32>( kind ), bCheckOnly ? 1u : 0u, &service ) == 0;
        } );
    }

    bool runLocalizationWithEditorModule( EditorModuleHeadlessSession& session, EditorLocalizationTask task, string_view poPath, string_view projectArgument )
    {
        const string poPathText( poPath );
        const string projectText( projectArgument );
        return EditorModuleHeadlessTasksInternal::callEditorModuleEntry<PFN_RunEditorLocalizationTask>(
            session, kRunEditorLocalizationTaskSymbol, "Localization tools", [task, &poPathText, &projectText]( PFN_RunEditorLocalizationTask pfnRun, const ISubsystemLocator& service )
        {
            return pfnRun( static_cast<uint32>( task ), poPathText.c_str(), projectText.c_str(), &service ) == 0;
        } );
    }

    bool validateAssetReferencesWithEditorModule( EditorModuleHeadlessSession& session )
    {
        return EditorModuleHeadlessTasksInternal::callEditorModuleEntry<PFN_CheckEditorAssetReferences>(
            session, kCheckEditorAssetReferencesSymbol, "Asset reference check", []( PFN_CheckEditorAssetReferences pfnCheck, const ISubsystemLocator& service )
        {
            return pfnCheck( &service ) == 0;
        } );
    }

    bool runUnitTestsWithEditorModule( EditorModuleHeadlessSession& session, const CommandLineManager& commandLine )
    {
        if ( commandLine.isAutomatedRun() == false )
        {
            SW_LOG_ERROR_ID( "EditorModuleHeadlessTasks.EditorTestsNeedUnattended", "--test-editor needs -unattended - the editor instance would read and write the user's editor state" );
            return false;
        }
        EditorAPI                 api{};
        const PFN_ExportEditorAPI pfnExport = reinterpret_cast<PFN_ExportEditorAPI>( session.findEntry( kEditorModuleSymbols._pExportSymbol, "Editor unit tests" ) );
        if ( pfnExport == nullptr || pfnExport( &api ) == false || session.loadExtensions() == false )
            return false;
        // 에디터 모듈이 보는 범위는 보통 실행(`GameModuleManager::fillModuleSubsystemScope`)과 같다 — 엔진 서비스, 모듈이 드러낸 서브시스템, 맡김 칸.
        SubsystemScope services;
        engine::bindModuleSubsystems( services, false );
        if ( const ModuleRegistrationHost* const pRegistrationHost = engine::findSubsystem<ModuleRegistrationHost>() )
            services.setParentScope( &pRegistrationHost->getHostModuleLocator() );
        if ( IModuleStateStore* const pStateStore = engine::findSubsystem<IModuleStateStore>() )
            (void)services.bind( pStateStore ); // 새 범위라 겹치지 않는다
        api.bindService( &services );
        const auto initializeHeadless = [&api]( EditorHandle editor )
        { return api.initializeHeadless( editor ); };
        EditorHandle editor = nullptr;
        bool         bPassed{ false };
        if ( ModuleInstanceUtil::createInstance( api, editor, initializeHeadless, nullptr, false, "Editor" ) )
        {
            bPassed = UnitTestRun::run( commandLine, "/Editor/", "editorApp" ); // 에디터 모듈은 모두 `Editor/` 폴더다(확장 · 플러그인도)
            api.requestBackgroundStop( editor );
        }
        ModuleInstanceUtil::destroyInstance( api, editor, false, sw::config::kTargetEditorModule ); // 타입 등록 · 라이브러리는 세션이 내린다
        return bPassed;
    }
} // namespace sw
#else
namespace sw
{
    // 정적 모듈 구성 — 에디터 모듈이 없다. 작업 표가 세션을 쥐지만 작업은 모듈을 찾기 전에 실패로 답한다(`build::kStaticModules`).
    EditorModuleHeadlessSession::EditorModuleHeadlessSession()
        : _listExtension{}
        , _pLibrary{ nullptr }
        , _loadCount{ 0 }
        , _entryCallCount{ 0 }
        , _bLoadAttempted{ SW_FALSE }
        , _bTypesRegistered{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorModuleHeadlessSession::~EditorModuleHeadlessSession() = default;

    void* EditorModuleHeadlessSession::findEntry( const utf8* /*pSymbol*/, const utf8* /*pPurpose*/ )
    {
        return nullptr;
    }

    bool EditorModuleHeadlessSession::loadExtensions()
    {
        return false;
    }
} // namespace sw
#endif // SW_WITH_DYNAMIC_MODULES

namespace sw
{
    namespace
    {
        /** @brief 작업 표의 본문입니다. */
        struct EditorModuleHeadlessTaskRun
        {
            /** @brief 인자 @p kArgument 가운데 하나라도 주어졌으면 true 입니다(줄의 "청해졌는가"). */
            template <CommandLineArgument... kArgument>
            static bool isAnyProvided( const CommandLineManager& commandLine )
            {
                return ( commandLine.isArgumentProvided( kArgument ) || ... );
            }

            /**
             * @brief 로컬라이제이션 도구(`--gather-text`, `--check-text`, `--import-po=<파일>`, `--export-po`, 프로젝트는 `-loc-project`)입니다.
             * @details 번역을 수집한 직후 교환할 수 있도록, 한 번의 실행에서 수집, 가져오기, 내보내기 순서로 처리합니다. 이 단계는 타입 공급자 모듈 뒤라 글 수집이 리플렉션 프로퍼티(`Meta = "Localizable"`)를 본다.
             */
            static bool runLocalizationTools( const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule )
            {
                bool   bGatherText = false;
                bool   bCheckText  = false;
                bool   bExportPo   = false;
                string importPoPath;
                string projectPath;
                commandLine.getArgument( CommandLineArgument::GATHER_TEXT, bGatherText );
                commandLine.getArgument( CommandLineArgument::CHECK_TEXT, bCheckText );
                commandLine.getArgument( CommandLineArgument::EXPORT_PO, bExportPo );
                commandLine.getArgument( CommandLineArgument::IMPORT_PO, importPoPath );
                commandLine.getArgument( CommandLineArgument::LOC_PROJECT, projectPath );
                if ( bGatherText == false && bCheckText == false && bExportPo == false && importPoPath.empty() )
                    return true; // 꺼서 준 인자(`--gather-text=false`)뿐이다 — 할 일이 없다

                if constexpr ( build::kStaticModules ) // 에디터 모듈의 일이다 — 정적 구성에는 에디터 모듈도 그것을 올리는 길도 없다
                {
                    SW_LOG_ERROR_ID( "App.LocalizationToolsNeedEditor", "Localization tools need the editor module, which a build with statically linked modules does not have - run them from a desktop Dev build." );
                    return false;
                }
                else
                {
                    bool bSucceeded = true;
                    if ( bGatherText || bCheckText )
                    {
                        const EditorLocalizationTask gatherTask = bCheckText ? EditorLocalizationTask::CheckText : EditorLocalizationTask::GatherText;
                        bSucceeded                              = runLocalizationWithEditorModule( editorModule, gatherTask, {}, projectPath ) && bSucceeded;
                    }
                    if ( importPoPath.empty() == false )
                        bSucceeded = runLocalizationWithEditorModule( editorModule, EditorLocalizationTask::ImportPo, importPoPath, projectPath ) && bSucceeded;
                    if ( bExportPo )
                        bSucceeded = runLocalizationWithEditorModule( editorModule, EditorLocalizationTask::ExportPo, {}, projectPath ) && bSucceeded;
                    return bSucceeded;
                }
            }

            /** @brief 에셋 참조 검사(`--check-asset-refs`)입니다. 글 에셋 · 테스트 CMake 의 경로 칸과 리플렉션 `AssetPath` 프로퍼티를 칸 목록(`Config/Dev/AssetReferenceFields.json`)으로 본다. */
            static bool runAssetReferenceCheck( const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule )
            {
                bool bCheck = false;
                commandLine.getArgument( CommandLineArgument::CHECK_ASSET_REFS, bCheck );
                if ( bCheck == false )
                    return true;                       // 꺼서 준 인자다
                if constexpr ( build::kStaticModules ) // 에디터 모듈의 일이다 — 정적 구성에는 에디터 모듈도 그것을 올리는 길도 없다
                {
                    SW_LOG_ERROR_ID( "App.AssetReferenceCheckNeedsEditor", "The asset reference check needs the editor module, which a build with statically linked modules does not have - run it from a desktop Dev build." );
                    return false;
                }
                else
                {
                    return validateAssetReferencesWithEditorModule( editorModule );
                }
            }

            /** @brief 에디터 단위 테스트(`--test-editor`)입니다. */
            static bool runEditorUnitTests( const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule )
            {
                bool bRun = false;
                commandLine.getArgument( CommandLineArgument::TEST_EDITOR, bRun );
                if ( bRun == false )
                    return true;                       // 꺼서 준 인자다
                if constexpr ( build::kStaticModules ) // 에디터 모듈의 일이다 — 정적 구성에는 에디터 모듈도 그것을 올리는 길도 없다
                {
                    SW_LOG_ERROR_ID( "App.EditorTestsNeedEditor", "Editor unit tests need the editor module, which a build with statically linked modules does not have - run them from a desktop Dev build." );
                    return false;
                }
                else
                {
                    return runUnitTestsWithEditorModule( editorModule, commandLine );
                }
            }

            /**
             * @brief 원본 임포트(`--import-<종류>`) 또는 대조만 하는 실행(`--check-<종류>`)입니다.
             * @tparam kKind 임포트 종류. @tparam kImportArgument 임포트 인자. @tparam kCheckArgument 대조만 하는 인자.
             */
            template <EditorImportKind kKind, CommandLineArgument kImportArgument, CommandLineArgument kCheckArgument>
            static bool runEditorImport( const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule )
            {
                bool bImport = false;
                bool bCheck  = false;
                commandLine.getArgument( kImportArgument, bImport );
                commandLine.getArgument( kCheckArgument, bCheck );
                if ( bImport == false && bCheck == false )
                    return true;                       // 꺼서 준 인자다
                if constexpr ( build::kStaticModules ) // 에디터 모듈의 일이다 — 정적 구성에는 에디터 모듈도 그것을 올리는 길도 없다
                {
                    SW_LOG_ERROR_ID( "App.AssetImportingNeedsEditor", "Asset importing needs the editor module, which a build with statically linked modules does not have - run it from a desktop Dev build." );
                    return false;
                }
                else
                {
                    // 변경 없는 임포트 · 대조가 원본과 산출물을 다시 읽지 않는다(git 색인처럼 크기 · 시각이 그대로인 파일만 건너뛴다 — 판정은 여전히 내용 해시다).
                    const LocalContentHashCacheScope hashCacheScope{ bCheck ? LocalContentHashCacheUse::LoadOnly : LocalContentHashCacheUse::LoadAndSave };
                    return importAssetsWithEditorModule( editorModule, kKind, bCheck );
                }
            }

            /** @brief 원본 임포트 · 대조 줄입니다 — 두 인자를 한 번만 적는다. 원본 임포트는 App 의 일이라 서버에 주면 기동이 실패한다. */
            template <EditorImportKind kKind, CommandLineArgument kImportArgument, CommandLineArgument kCheckArgument>
            static constexpr EditorModuleHeadlessTask makeImportTask()
            {
                return { &isAnyProvided<kImportArgument, kCheckArgument>, HeadlessTaskHost::Client, &runEditorImport<kKind, kImportArgument, kCheckArgument> };
            }
        };

        /**
         * @brief 작업 표입니다. 줄 순서가 실행 순서다(수집 · PO 교환 → 참조 검사 → 임포트). 글 도구 · 참조 검사는 어느 호스트에서나 헤드리스로 서고(돌리는 것은 App), 원본 임포트 · 대조는 App 만 받는다.
         * @details 본문 구조체 밖에 둔다 — 줄을 만드는 constexpr 함수(`makeImportTask`)는 그 구조체가 닫힌 뒤에야 상수식에서 부를 수 있다.
         */
        using EditorModuleTaskRun                                         = EditorModuleHeadlessTaskRun;
        constexpr EditorModuleHeadlessTask kArrEditorModuleHeadlessTask[] = {
            { &EditorModuleTaskRun::isAnyProvided<CommandLineArgument::GATHER_TEXT, CommandLineArgument::CHECK_TEXT, CommandLineArgument::EXPORT_PO, CommandLineArgument::IMPORT_PO>, HeadlessTaskHost::Any,
             &EditorModuleTaskRun::runLocalizationTools },
            { &EditorModuleTaskRun::isAnyProvided<CommandLineArgument::CHECK_ASSET_REFS>, HeadlessTaskHost::Any, &EditorModuleTaskRun::runAssetReferenceCheck },
            { &EditorModuleTaskRun::isAnyProvided<CommandLineArgument::TEST_EDITOR>, HeadlessTaskHost::Client, &EditorModuleTaskRun::runEditorUnitTests },
            EditorModuleTaskRun::makeImportTask<EditorImportKind::Texture, CommandLineArgument::IMPORT_TEXTURES, CommandLineArgument::CHECK_TEXTURES>(),
            EditorModuleTaskRun::makeImportTask<EditorImportKind::TextureVariant, CommandLineArgument::COOK_TEXTURE_VARIANTS, CommandLineArgument::CHECK_TEXTURE_VARIANTS>(),
            EditorModuleTaskRun::makeImportTask<EditorImportKind::Model, CommandLineArgument::IMPORT_MODELS, CommandLineArgument::CHECK_MODELS>(),
            EditorModuleTaskRun::makeImportTask<EditorImportKind::Heightfield, CommandLineArgument::IMPORT_HEIGHTFIELDS, CommandLineArgument::CHECK_HEIGHTFIELDS>(),
        };

        /** @brief 엔진 `Headless` 단계에 올리는 줄의 판정 · 본문입니다. */
        struct EditorModuleHeadlessTaskEntry
        {
            /** @brief 표에서 호스트가 @p kHost 인 줄이 하나라도 청해졌으면 true 입니다 — 엔진 `Headless` 단계의 등록 줄이 호스트 값마다 하나씩 이것으로 묻는다. */
            template <HeadlessTaskHost kHost>
            static bool isRequestedOnHost( const CommandLineManager& commandLine )
            {
                for ( const EditorModuleHeadlessTask& task : kArrEditorModuleHeadlessTask )
                {
                    if ( task._host == kHost && task._pIsRequested( commandLine ) )
                        return true;
                }
                return false;
            }

            /** @brief 엔진 단계의 본문 — 창 · RHI 없이 세우기만 한다. 모듈을 올려 돌리는 것은 엔진이 선 뒤의 호스트다(`EditorModuleHeadlessTask::runRequested`). */
            static bool enterHeadless( HeadlessTaskContext& /*context*/ )
            {
                SW_LOG_INFO_ID( "Headless.StartingHeadlessEditorTools", "Starting Headless (editor tools: localization or asset import/check)..." );
                return true;
            }
        };
    } // namespace

    // 엔진의 `Headless` 단계에 올리는 줄 — 호스트 값마다 하나이고 무엇이 청해졌는지는 위 표가 답한다(새 작업은 표에만 더한다).
    using EditorModuleTaskEntry = EditorModuleHeadlessTaskEntry;
    SW_HEADLESS_TASK( editorModuleTasks, "EditorModuleTasks", HeadlessTaskPriority::kEditorTools, Any, &EditorModuleTaskEntry::isRequestedOnHost<HeadlessTaskHost::Any>, &EditorModuleTaskEntry::enterHeadless );
    SW_HEADLESS_TASK( editorModuleClientTasks, "EditorModuleClientTasks", HeadlessTaskPriority::kEditorTools, Client, &EditorModuleTaskEntry::isRequestedOnHost<HeadlessTaskHost::Client>,
                      &EditorModuleTaskEntry::enterHeadless );
    SW_HEADLESS_TASK( editorModuleServerTasks, "EditorModuleServerTasks", HeadlessTaskPriority::kEditorTools, DedicatedServer, &EditorModuleTaskEntry::isRequestedOnHost<HeadlessTaskHost::DedicatedServer>,
                      &EditorModuleTaskEntry::enterHeadless );
} // namespace sw

namespace sw
{
    bool EditorModuleHeadlessTask::runRequested( const CommandLineManager& commandLine )
    {
        EditorModuleHeadlessSession editorModule; // 에디터 모듈 작업이 여럿이어도 모듈은 한 번 올리고 표가 끝나면 내린다
        return runTable( kArrEditorModuleHeadlessTask, commandLine, editorModule );
    }

    bool EditorModuleHeadlessTask::runTable( span<const EditorModuleHeadlessTask> listTask, const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule )
    {
        bool bSucceeded = true;
        for ( const EditorModuleHeadlessTask& task : listTask )
        {
            if ( task._pIsRequested( commandLine ) && task._pRun( commandLine, editorModule ) == false )
                bSucceeded = false;
        }
        return bSucceeded;
    }
} // namespace sw
