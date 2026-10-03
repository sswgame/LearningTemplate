#include "pch.h"

#include "Engine/Module/ModuleTypeRegistry.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
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

        /**
         * @brief enum 전역 변수의 글 값을 리플렉션 열거자로 읽습니다(`GlobalVariableEnumTextParser`).
         * @details 타입 이름은 정의 매크로의 `#enumType` 이라 인턴해도 된다. `findInterned` 로 찾으면 그 이름을 아직 아무도 인턴하지 않은
         *          기동 시점(App)에 빈 해시가 나와 모든 이름이 "없다" 가 된다 — 시험 프로세스는 다른 시험이 먼저 인턴해 두어 가려진다.
         */
        [[nodiscard]] bool parseGlobalVariableEnumText( string_view enumType, string_view text, int32& outValue )
        {
            const EnumInfo* pEnum = engine::getTypeRegistry().findEnum( hashed_string( enumType.data(), static_cast<uint32>( enumType.size() ) ) );
            int64           value{ 0 };
            if ( pEnum == nullptr || pEnum->tryParseText( text, value ) == false )
                return false;
            outValue = static_cast<int32>( value );
            return true;
        }
    } // namespace

    namespace engine
    {
        void registerModuleTypes( string_view moduleName )
        {
            // 방금 로드된 DLL 의 정적 등록기들이 전역 머리에 매달려 있다. 그것을 걷어서 넘긴다.
            // 이미 등록한 모듈인데 머리에 새 등록기가 있으면 **다른 이미지**의 것이다 — 핫 리로드는 올린 직후 머리를 따로 걷어 넘기므로(인자 있는
            // 오버로드) 이 자리에서는 비어 있어야 한다. 지연 로드로 늦게 올라온 공용 모듈이 이렇게 남의 이름으로 들어간다. 알린다.
            if ( getModuleHeadCache().contains( string{ moduleName } ) &&
                 TypeRegistrar::getHead() != nullptr )
            {
                SW_LOG_WARNING( "Registrars of another image were waiting when '%#' registered again — they are attributed to '%#'. "
                                "Load shared modules explicitly first (LiveReloadManager::loadSharedModule).",
                                moduleName, moduleName );
            }
            // **캐시 병합은 아래 오버로드가 한 자리에서 한다.**
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
                    // 살아 있는 컴포넌트에 기본값을 다시 찍지 않는다 — 기본값은 만들 때 한 번이다(`ComponentDefaults`). 여기서 덮어쓰면
                    // 게임이 바꾼 값이 모듈 로드마다 기본값으로 돌아간다.
                    // TypeInfo 주소는 고정이라(`TypeRegistry`) 다시 묶을 것도 없다. 틱 항목만 다시 짓게 한다.
                    scene->getObjectManager()->markTickStagesDirty();
                }
            }
        }

        bool bindGlobalVariableEnumNames()
        {
            GlobalVariableManager::setEnumTextParser( SW_DELEGATE_FUNCTION( GlobalVariableEnumTextParser, parseGlobalVariableEnumText ) );
            return getGlobalVariableManager().applyPendingEnumText();
        }

        void unbindGlobalVariableEnumNames()
        {
            GlobalVariableManager::setEnumTextParser( GlobalVariableEnumTextParser{} );
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
#endif
    } // namespace engine
} // namespace sw
