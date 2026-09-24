#include "pch.h"

#include "Engine/Module/ModuleTypeRegistry.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"

SW_LOG_CALLER( "ModuleTypeRegistry" );
namespace sw
{
    namespace
    {
        struct ModuleHeadRecord
        {
            TypeRegistrar*                 _pTypeHead{ nullptr };
            EnumRegistrar*                 _pEnumHead{ nullptr };
            sw::ComponentFactoryRegistrar* _pFactoryHead{ nullptr };
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
            // **캐시 병합은 아래 오버로드가 한 자리에서 한다.** 예전에는 같은 18줄이 여기에도
            // 한 벌 더 있었고(조건만 뒤집힌 같은 로직), 그러고 나서 아래를 불러 또 병합했다.
            registerModuleTypes( moduleName,
                                 TypeRegistrar::getHead(),
                                 EnumRegistrar::getHead(),
                                 sw::ComponentFactoryRegistrar::getHead(),
                                 GlobalVariableRegistrar::getHead() );

            // 소비했으므로 비운다. 다음 DLL 이 자기 것만 매달도록.
            TypeRegistrar::getHead()                 = nullptr;
            EnumRegistrar::getHead()                 = nullptr;
            sw::ComponentFactoryRegistrar::getHead() = nullptr;
            GlobalVariableRegistrar::getHead()       = nullptr;
        }

        void registerModuleTypes( string_view                    moduleName,
                                  TypeRegistrar*                 pTypeHead,
                                  EnumRegistrar*                 pEnumHead,
                                  sw::ComponentFactoryRegistrar* pFactoryHead,
                                  GlobalVariableRegistrar*       pVariableHead )
        {
            // 전역 변수는 새로 뗀 것만 올린다(아래 캐시에 넣지 않는다). 모듈이 자기 변수를 읽기 전에 커맨드라인 보류값이 여기서 적용된다.
            if ( pVariableHead != nullptr )
                getGlobalVariableManager().registerPendingVariables( moduleName, pVariableHead );

            // 캐시와 인자를 합치는 **유일한 자리**다. 새로 받은 머리가 있으면 캐시를 갱신하고,
            // 없으면 캐시에 남아 있던 것을 쓴다(리로드로 같은 모듈이 다시 올 때의 경로다).
            auto&        cache  = getModuleHeadCache();
            const string modStr = string{ moduleName };
            const auto   it     = cache.find( modStr );
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

                if ( pFactoryHead != nullptr )
                    it->second._pFactoryHead = pFactoryHead;
                else
                    pFactoryHead = it->second._pFactoryHead;
            }
            else if ( pTypeHead != nullptr || pEnumHead != nullptr || pFactoryHead != nullptr )
            {
                cache[modStr] = ModuleHeadRecord{ pTypeHead, pEnumHead, pFactoryHead };
            }

            getTypeRegistry().registerPendingTypes( moduleName, pTypeHead, pEnumHead );
            GameObjectManager::registerModuleFactoryHead( moduleName, pFactoryHead );

            for ( const auto& scene : getSceneManager().getLoadedScenes() )
            {
                if ( scene && scene->getObjectManager() )
                {
                    scene->getObjectManager()->registerPendingFactories( moduleName, pFactoryHead );
                    scene->getObjectManager()->rebindAllCachedTypeInfo();
                }
            }
        }

#if !defined( SW_SHIPPING )
        void unregisterModuleTypes( string_view moduleName )
        {
            getModuleHeadCache().erase( string{ moduleName } );
            GameObjectManager::unregisterModuleFactoryHead( moduleName );
            // 씬은 엔진이 소유해 모듈보다 오래 산다. 이 모듈 타입의 인스턴스가 남아 있으면 vtable 이 사라진 객체가 된다.
            // 팩토리를 걷기 **전에** 인스턴스부터 지운다(소멸자가 아직 있는 동안).
            for ( const auto& scene : getSceneManager().getLoadedScenes() )
            {
                if ( scene && scene->getObjectManager() )
                {
                    scene->getObjectManager()->destroyComponentsOfModule( moduleName );
                    scene->getObjectManager()->unregisterFactoriesByModule( moduleName );
                }
            }
            getTypeRegistry().unregisterTypesByModule( moduleName );
            getGlobalVariableManager().unregisterVariablesByModule( moduleName );
        }

        uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd )
        {
            if ( areEngineServicesBound() == false || pBegin == nullptr || pEnd == nullptr )
                return 0;

            // 모듈은 자기가 단 것을 스스로 떼야 한다(에디터는 ImGuiEditor::shutdown · ~ConsolePanel 에서 뗀다). 여기서 뗀 것이 있으면
            // 그 정리가 빠졌다는 뜻이라 경고로 남긴다. 늘 0 이어야 한다.
            uint32       stuckEntryCount{ 0 };
            const uint32 eventCount = getEventDispatcher().releaseCodeWithin( pBegin, pEnd, stuckEntryCount );
            if ( eventCount > 0 )
                SW_LOG_WARNING( "Module %# left %# event subscription(s) behind — released them before unloading its image", moduleName, eventCount );
            if ( stuckEntryCount > 0 )
                SW_LOG_ERROR( "Module %# created %# event channel(s) that other code still subscribes to — publishing them after the unload would jump into the unloaded image",
                              moduleName, stuckEntryCount );

            const uint32 listenerCount = Logger::releaseGlobalListenerCodeWithin( pBegin, pEnd );
            if ( listenerCount > 0 )
                SW_LOG_WARNING( "Module %# left %# log listener(s) behind — released them before unloading its image", moduleName, listenerCount );

            // Undo 스택은 선택 서비스다(Shipping · 일부 호스트에는 없다).
            CommandStack* pCommandStack = getBoundEngineServices()._pCommandStack;
            const uint32  commandCount  = pCommandStack != nullptr ? pCommandStack->releaseCodeWithin( pBegin, pEnd ) : 0;
            if ( commandCount > 0 )
                SW_LOG_WARNING( "Module %# left %# undo command(s) behind — cleared the undo stack before unloading its image", moduleName, commandCount );

            IWindow*     pWindow     = IWindow::getActiveWindow();
            const uint32 windowCount = pWindow != nullptr ? pWindow->releaseCodeWithin( pBegin, pEnd ) : 0;
            if ( windowCount > 0 )
                SW_LOG_WARNING( "Module %# left %# window handler(s) behind — released them before unloading its image", moduleName, windowCount );

            return eventCount + listenerCount + commandCount + windowCount;
        }
#endif
    } // namespace engine
} // namespace sw
