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
// 부르는 자리(`AppHeadlessTasks.cpp`)는 `build::kStaticModules` 로 가른다 — 버려지는 갈래라 정의가 없어도 된다(docs/agents/CppStyle.md "Build configuration switches" 첫 줄).
#if SW_WITH_DYNAMIC_MODULES
    #include "Core/File/FileUtil.h"
    #include "Core/Module/ModuleLibraryUtil.h"
    #include "Core/Path/AbsolutePath.h"

namespace sw
{
    EditorModuleHeadlessSession::EditorModuleHeadlessSession()
        : _pLibrary{ nullptr }
        , _loadCount{ 0 }
        , _entryCallCount{ 0 }
        , _bLoadAttempted{ SW_FALSE }
        , _bTypesRegistered{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorModuleHeadlessSession::~EditorModuleHeadlessSession()
    {
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
            _bLoadAttempted                          = SW_TRUE;
            const string                 modulePath  = ModuleLibraryUtil::findModuleLibraryPath( sw::config::kTargetEditorModule );
            const optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( modulePath );
            const ModuleLibraryHandle    library{ libraryPath.has_value() && FileUtil::exists( FileUtil::toDiskPath( modulePath ) ) ? ModuleLibraryUtil::loadLibraryAt( *libraryPath ) : nullptr };
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
            if ( ModuleInstanceUtil::matchesModuleABI( library, ClientModuleManager::getModuleSymbols() ) == false )
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
                engine::bindModuleServices( service, false );
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
} // namespace sw
#else
namespace sw
{
    // 정적 모듈 구성 — 에디터 모듈이 없다. 작업 표가 세션을 쥐지만 작업은 모듈을 찾기 전에 실패로 답한다(`build::kStaticModules`).
    EditorModuleHeadlessSession::EditorModuleHeadlessSession()
        : _pLibrary{ nullptr }
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
} // namespace sw
#endif // SW_WITH_DYNAMIC_MODULES
