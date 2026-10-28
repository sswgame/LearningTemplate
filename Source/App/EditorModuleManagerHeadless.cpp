#include "pch.h"

#include "App/EditorModuleManager.h"

#if !defined( SW_SHIPPING )
    #include "Core/File/FileUtil.h"
    #include "Core/Module/ModuleLibraryUtil.h"

    #include "Engine/Foundation/EngineServices.h"
    #include "Engine/Module/ModuleTypeRegistry.h"

    #include "ModuleManager/ModuleInstanceUtil.h"

    #include "Core/Subsystem/SubsystemScope.h"

    #include "sw/config/ConfigConstants.h"
#endif

#if !defined( SW_SHIPPING )
namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다. 인스턴스 없이 에디터 모듈을 올려 부르는 헤드리스 경로입니다(Shipping 에는 에디터 모듈이 없다). */
        struct EditorModuleManagerHeadlessInternal
        {
            /**
             * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 진입점 @p pSymbol 하나를 부르고 내립니다(헤드리스 임포트 · 로컬라이제이션 공통).
             * @details 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 걷습니다. @p call 은 `( TEntryFn, const ISubsystemLocator& ) -> bool` 이고 엔진 서비스 범위를 받습니다.
             * @param symbols 에디터 모듈의 ABI 심볼 이름(`EditorModuleManager::getModuleSymbols`)입니다.
             * @param pPurpose 로그에 쓰는 작업 이름입니다(`"Asset importing"`).
             */
            template <typename TEntryFn, typename TCall>
            [[nodiscard]] static bool callEditorModuleEntry( const ModuleAPISymbols& symbols, const utf8* pSymbol, const utf8* pPurpose, TCall&& call )
            {
                const string modulePath     = ModuleLibraryUtil::findModuleLibraryPath( sw::config::kTargetEditorModule );
                void* const  pLibraryModule = FileUtil::exists( modulePath ) ? ModuleLibraryUtil::loadDynamicLibrary( modulePath ) : nullptr;
                if ( pLibraryModule == nullptr )
                {
                    SW_LOG_ERROR( "%# needs the editor module in Bin/Modules: %#", pPurpose, modulePath.c_str() );
                    return false;
                }

                // 로드하는 순간 모듈의 정적 등록기가 전역 헤드에 매달린다. 모듈 이름으로 등록해 두어야 언로드하기 전에 해제할 수 있다.
                engine::registerModuleTypes( sw::config::kTargetEditorModule );

                bool bSucceeded = false;
                if ( ModuleInstanceUtil::matchesModuleABI( pLibraryModule, symbols ) )
                {
                    const TEntryFn pfnEntry = reinterpret_cast<TEntryFn>( ModuleLibraryUtil::getDynamicSymbol( pLibraryModule, pSymbol ) );
                    if ( pfnEntry == nullptr )
                        SW_LOG_ERROR( "The editor module does not export %#", pSymbol );
                    else
                    {
                        SubsystemScope service;
                        engine::bindModuleServices( service, false );
                        bSucceeded = call( pfnEntry, static_cast<const ISubsystemLocator&>( service ) );
                    }
                }

                engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
                // 언로드하지 못하면(다른 코드가 아직 그 이미지의 이벤트 채널을 구독한다) 프로세스 끝까지 로드된 채로 남을 뿐이다. 이유는 경고로 남는다.
                (void)ModuleLibraryUtil::unloadModuleLibrary( sw::config::kTargetEditorModule, pLibraryModule );
                return bSucceeded;
            }
        };
    } // namespace
} // namespace sw
#endif

namespace sw
{
    SW_LOG_CALLER( "EditorModuleManager" );

    bool EditorModuleManager::importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly )
    {
#if defined( SW_SHIPPING )
        (void)kind;
        (void)bCheckOnly;
        SW_LOG_ERROR( "Asset importing needs the editor module, which a Shipping build does not have - run it from a Dev build." );
        return false;
#else
        // 인스턴스 없이 부르므로 엔진 서비스 범위를 직접 넘긴다. 임포터가 작업 시스템으로 압축을 나눈다.
        return EditorModuleManagerHeadlessInternal::callEditorModuleEntry<PFN_ImportEditorAssets>(
            getModuleSymbols(), kImportEditorAssetsSymbol, "Asset importing", [kind, bCheckOnly]( PFN_ImportEditorAssets pfnImport, const ISubsystemLocator& service )
        {
            return pfnImport( static_cast<uint32>( kind ), bCheckOnly ? 1u : 0u, &service ) == 0;
        } );
#endif
    }

    bool EditorModuleManager::runLocalizationWithEditorModule( EditorLocalizationTask task, string_view poPath, string_view projectArgument )
    {
#if defined( SW_SHIPPING )
        (void)task;
        (void)poPath;
        (void)projectArgument;
        SW_LOG_ERROR( "Localization tools need the editor module, which a Shipping build does not have - run them from a Dev build." );
        return false;
#else
        const string poPathText( poPath );
        const string projectText( projectArgument );
        return EditorModuleManagerHeadlessInternal::callEditorModuleEntry<PFN_RunEditorLocalizationTask>(
            getModuleSymbols(), kRunEditorLocalizationTaskSymbol, "Localization tools",
            [task, &poPathText, &projectText]( PFN_RunEditorLocalizationTask pfnRun, const ISubsystemLocator& service )
        {
            return pfnRun( static_cast<uint32>( task ), poPathText.c_str(), projectText.c_str(), &service ) == 0;
        } );
#endif
    }
} // namespace sw
