#include "pch.h"

#include "Engine/Module/ModuleTypeRegistry.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Module/ModuleCodeHolder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

SW_LOG_CALLER( "ModuleTypeRegistry" );
namespace sw
{
    namespace
    {
        struct ModuleHeadRecord
        {
            TypeRegistrar* _pTypeHead{ nullptr };
            EnumRegistrar* _pEnumHead{ nullptr };
        };

        unordered_map<string, ModuleHeadRecord>& getModuleHeadCache()
        {
            static unordered_map<string, ModuleHeadRecord> s_mapModuleHead;
            return s_mapModuleHead;
        }
    } // namespace

    namespace engine
    {
        void registerModuleTypes( string_view moduleName )
        {
            // 방금 로드된 DLL 의 정적 등록기들이 전역 머리에 매달려 있다. 그것을 걷어서 넘긴다.
            // 이미 등록한 모듈인데 머리에 새 등록기가 있으면 **다른 이미지**의 것이다 — 핫 리로드는 올린 직후 머리를 따로 걷어 넘기므로(인자 있는
            // 오버로드) 이 자리에서는 비어 있어야 한다. 지연 로드로 늦게 올라온 공용 모듈이 이렇게 남의 이름으로 들어갔다. 알린다.
            if ( getModuleHeadCache().contains( string{ moduleName } ) &&
                 TypeRegistrar::getHead() != nullptr )
            {
                SW_LOG_WARNING( "Registrars of another image were waiting when '%#' registered again — they are attributed to '%#'. "
                                "Load shared modules explicitly first (LiveReloadManager::loadSharedModule).",
                                moduleName, moduleName );
            }
            // **캐시 병합은 아래 오버로드가 한 자리에서 한다.** 예전에는 같은 18줄이 여기에도
            // 한 벌 더 있었고(조건만 뒤집힌 같은 로직), 그러고 나서 아래를 불러 또 병합했다.
            registerModuleTypes( moduleName, TypeRegistrar::getHead(), EnumRegistrar::getHead(), GlobalVariableRegistrar::getHead() );

            // 소비했으므로 비운다. 다음 DLL 이 자기 것만 매달도록.
            TypeRegistrar::getHead()           = nullptr;
            EnumRegistrar::getHead()           = nullptr;
            GlobalVariableRegistrar::getHead() = nullptr;
        }

        void registerModuleTypes( string_view moduleName, TypeRegistrar* pTypeHead, EnumRegistrar* pEnumHead, GlobalVariableRegistrar* pVariableHead )
        {
            SW_MEMORY_SCOPE( Reflection );
            // 전역 변수는 새로 뗀 것만 올린다(아래 캐시에 넣지 않는다). 모듈이 자기 변수를 읽기 전에 커맨드라인 보류값이 여기서 적용된다.
            if ( pVariableHead != nullptr )
                getGlobalVariableManager().registerPendingVariables( moduleName, pVariableHead );

            // 캐시와 인자를 합치는 **유일한 자리**다. 새로 받은 머리가 있으면 캐시를 갱신하고,
            // 없으면 캐시에 남아 있던 것을 쓴다(리로드로 같은 모듈이 다시 올 때의 경로다).
            auto&        cache           = getModuleHeadCache();
            const string ownedModuleName = string{ moduleName };
            const auto   it              = cache.find( ownedModuleName );
            if ( it != cache.end() )
            {
                if ( pTypeHead != nullptr )
                    it->second._pTypeHead = pTypeHead;
                else
                    pTypeHead = it->second._pTypeHead;

                if ( pEnumHead != nullptr )
                    it->second._pEnumHead = pEnumHead;
                else
                    pEnumHead = it->second._pEnumHead;
            }
            else if ( pTypeHead != nullptr || pEnumHead != nullptr )
            {
                cache[ownedModuleName] = ModuleHeadRecord{ pTypeHead, pEnumHead };
            }

            // 컴포넌트 생성 함수는 타입과 함께 오른다(`TypeInfo::_addComponent`) — 씬마다 팩토리 표를 다시 채울 일이 없다.
            getTypeRegistry().registerPendingTypes( moduleName, pTypeHead, pEnumHead );

            for ( const auto& scene : getSceneManager().getLoadedScenes() )
            {
                if ( scene && scene->getObjectManager() )
                {
                    // 살아 있는 컴포넌트에 기본값을 다시 찍지 않는다 — 기본값은 만들 때 한 번이다(`ComponentDefaults`). 예전에는 여기서
                    // (`rebindAllCachedTypeInfo`) 씬의 모든 컴포넌트에 덮어써 게임이 바꾼 값이 모듈 로드마다 기본값으로 돌아갔다.
                    // TypeInfo 주소는 고정이라(`TypeRegistry`) 다시 묶을 것도 없다. 틱 항목만 다시 짓게 한다.
                    scene->getObjectManager()->markTickStagesDirty();
                }
            }
        }

#if !defined( SW_SHIPPING )
        void unregisterModuleTypes( string_view moduleName )
        {
            getModuleHeadCache().erase( string{ moduleName } );
            // 씬은 엔진이 소유해 모듈보다 오래 산다. 이 모듈 타입의 인스턴스가 남아 있으면 vtable 이 사라진 객체가 된다.
            // 타입(생성 함수 포함)을 걷기 **전에** 인스턴스부터 지운다(소멸자가 아직 있는 동안).
            for ( const auto& scene : getSceneManager().getLoadedScenes() )
            {
                if ( scene && scene->getObjectManager() )
                    scene->getObjectManager()->destroyComponentsOfModule( moduleName );
            }
            getTypeRegistry().unregisterTypesByModule( moduleName );
            getGlobalVariableManager().unregisterVariablesByModule( moduleName );
        }

        uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped )
        {
            if ( pOutKeepImageMapped != nullptr )
                *pOutKeepImageMapped = false;

            // 모듈은 자기가 단 것을 스스로 떼야 한다(에디터는 ImGuiEditor::shutdown · ~ConsolePanel 에서 뗀다). 여기서 뗀 것이 있으면
            // 그 정리가 빠졌다는 뜻이라 경고로 남긴다. 늘 0 이어야 한다. 훑는 대상은 살아 있는 보유자 전부다(`IModuleCodeHolder`).
            vector<IModuleCodeHolder::ReleaseResult> listResult;
            IModuleCodeHolder::releaseAllWithin( pBegin, pEnd, listResult );

            uint32 releasedCount{ 0 };
            for ( const IModuleCodeHolder::ReleaseResult& result : listResult )
            {
                releasedCount += result._releasedCount;
                if ( result._releasedCount > 0 )
                    SW_LOG_WARNING( "Module %# left %# %# behind — released them before unloading its image", moduleName, result._releasedCount, result._pHolderName );
                // 떼어 낼 수 없는 것(다른 코드가 아직 구독하는 이 이미지의 이벤트 채널)이 남았다. 내리면 다음 발행 · 종료 때 내려간 코드로 뛴다.
                if ( result._bKeepImageMapped )
                {
                    SW_LOG_WARNING( "Module %# is still referenced by %# that other code uses — keeping its image mapped until exit", moduleName, result._pHolderName );
                    if ( pOutKeepImageMapped != nullptr )
                        *pOutKeepImageMapped = true;
                }
            }
            return releasedCount;
        }

        bool unloadModuleImage( string_view moduleName, void* pHandle )
        {
            if ( pHandle == nullptr )
                return false;

            const void* pBegin{ nullptr };
            const void* pEnd{ nullptr };
            bool        bKeepImageMapped{ false };
            if ( FileUtil::findDynamicLibraryRange( pHandle, pBegin, pEnd ) )
                (void)releaseModuleCode( moduleName, pBegin, pEnd, &bKeepImageMapped ); // 뗀 것은 releaseModuleCode 가 경고로 남긴다
            if ( bKeepImageMapped )
                return false;

            // 이 이미지가 끌어온 의존 이미지가 함께 내려가지 않게 한다. 그 이미지의 코드를 쥔 등록은 여기서 뗄 수 없다.
            (void)FileUtil::pinDynamicLibraryDependencies( pHandle );
            FileUtil::unloadDynamicLibrary( pHandle );
            return true;
        }
#endif
    } // namespace engine
} // namespace sw
