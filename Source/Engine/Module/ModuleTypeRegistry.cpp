#include "pch.h"

#include "Engine/Module/ModuleTypeRegistry.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Module/ModuleObjectRegistrar.h"
#include "Engine/Module/ModuleRegistrationHost.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Object/Component/Component.h"
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
        using ModuleHeadRecord = ModuleRegistrationHost::HeadRecord;

        /**
         * @brief 모듈마다의 등록 상태를 가진 서브시스템입니다(엔진 서비스 범위). 모듈 등록 · 해제는 엔진 서비스가 선 동안에만 일어난다(아래가 서비스를 부른다).
         * @details 없으면 엔진을 세우지 않은 실행의 잘못이라 모든 구성에서 멈춘다 — 예전에는 소유자 없는 엔진 수명 맵 둘이 이 상태를 들었다.
         */
        ModuleRegistrationHost& getRegistrationHost()
        {
            ModuleRegistrationHost* const pHost = engine::findSubsystem<ModuleRegistrationHost>();
            if ( pHost == nullptr )
                ::sw::internal::reportMissingRequiredService( "ModuleRegistrationHost" );
            return *pHost;
        }

        unordered_map<string, ModuleHeadRecord>& getModuleHeadCache()
        {
            return getRegistrationHost().getHeadCache();
        }

        /** @brief @p pHead 의 등록자를 만든 순서(정적 초기화 순서)로 붙이고 @p moduleName 아래 기록합니다. */
        void attachModuleObjects( string_view moduleName, ModuleObjectRegistrar* pHead )
        {
            vector<ModuleObjectRegistrar*> listRegistrar;
            for ( ModuleObjectRegistrar* pRegistrar = pHead; pRegistrar != nullptr; pRegistrar = pRegistrar->getNext() )
            {
                listRegistrar.push_back( pRegistrar );
            }
            vector<ModuleObjectRegistrar*>& listAttached = getRegistrationHost().getAttachedObjects()[string{ moduleName }];
            for ( size_t index = listRegistrar.size(); index > 0; --index )
            {
                listRegistrar[index - 1]->attach();
                listAttached.push_back( listRegistrar[index - 1] );
            }
        }

        /** @brief @p moduleName 아래 붙인 등록자를 차례(`getDetachPass`)대로, 같은 차례 안에서는 붙인 역순으로 뗍니다. */
        void detachModuleObjects( string_view moduleName )
        {
            unordered_map<string, vector<ModuleObjectRegistrar*>>& mapRegistrar = getRegistrationHost().getAttachedObjects();
            const auto                                             it           = mapRegistrar.find( string{ moduleName } );
            if ( it == mapRegistrar.end() )
                return;
            const vector<ModuleObjectRegistrar*> listAttached = std::move( it->second );
            mapRegistrar.erase( it );
            uint32 lastPass{ 0 };
            for ( const ModuleObjectRegistrar* pRegistrar : listAttached )
            {
                lastPass = MathUtil::max( lastPass, pRegistrar->getDetachPass() );
            }
            for ( uint32 pass = 0; pass <= lastPass; ++pass )
            {
                for ( size_t index = listAttached.size(); index > 0; --index )
                {
                    if ( listAttached[index - 1]->getDetachPass() == pass )
                        listAttached[index - 1]->detach();
                }
            }
        }

        /**
         * @brief enum 전역 변수의 글 값을 리플렉션 열거자로 읽습니다(`GlobalVariableEnumTextParser`).
         * @details 타입 이름은 정의 매크로의 `#enumType` 이라 인턴해도 된다. `findInterned` 로 찾으면 그 이름을 아직 아무도 인턴하지 않은
         *          기동 시점(App)에 빈 해시가 나와 모든 이름이 "없다" 가 된다 — 테스트 프로세스는 다른 테스트가 먼저 인턴해 두어 가려진다.
         */
        [[nodiscard]] bool parseGlobalVariableEnumText( string_view enumType, string_view text, int32& outValue )
        {
            const EnumInfo* pEnum = engine::getSubsystem<TypeRegistry>().findEnum( hashed_string( enumType.data(), static_cast<uint32>( enumType.size() ) ) );
            int64           value{ 0 };
            if ( pEnum == nullptr || pEnum->tryParseText( text, value ) == false )
                return false;
            outValue = static_cast<int32>( value );
            return true;
        }
    } // namespace

    namespace engine
    {
        bool verifyModuleRegistrars( string_view moduleName, const ModuleRegistrarHeads& heads )
        {
            bool bMatched = true;
            for ( const GlobalVariableRegistrar* pRegistrar = heads._pVariable; pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
            {
                if ( moduleName == string_view{ pRegistrar->_pModuleName } )
                    continue;
                SW_LOG_ERROR( "Global variable %# belongs to module '%#' but was loaded with '%#' — load that module first (LiveReloadManager::loadSharedModule)",
                              pRegistrar->_pName, pRegistrar->_pModuleName, moduleName );
                bMatched = false;
            }
            for ( const AssetDataKindRegistrar* pRegistrar = heads._pDataKind; pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
            {
                if ( moduleName == string_view{ pRegistrar->_pModuleName } )
                    continue;
                SW_LOG_ERROR( "Resource data kind %# belongs to module '%#' but was loaded with '%#' — load that module first (LiveReloadManager::loadSharedModule)",
                              pRegistrar->_pKind->_pLabel, pRegistrar->_pModuleName, moduleName );
                bMatched = false;
            }
            return bMatched;
        }

        bool registerModuleTypes( string_view moduleName )
        {
            // 방금 로드된 DLL 의 정적 등록기들이 전역 머리에 매달려 있다. 걷어서 비운다 — 다음 DLL 이 자기 것만 매달도록(실패해도 남기지 않는다).
            const ModuleRegistrarHeads heads = peekGlobalRegistrarHeads();
            setGlobalRegistrarHeads( ModuleRegistrarHeads{} );
            if ( verifyModuleRegistrars( moduleName, heads ) == false )
                return false;
            // **캐시 병합은 아래 오버로드가 한 자리에서 한다.**
            registerModuleTypes( moduleName, heads );
            return true;
        }

        void registerHostImageTypes()
        {
            const ModuleRegistrarHeads heads = peekGlobalRegistrarHeads();
            setGlobalRegistrarHeads( ModuleRegistrarHeads{} );
            registerModuleTypes( "Engine", heads ); // 타입 머리 캐시 · 모듈 서브시스템 자리의 이름 — 호스트 이미지는 내리지 않는다
        }

        ModuleRegistrarHeads peekGlobalRegistrarHeads()
        {
            ModuleRegistrarHeads heads;
            heads._pType      = TypeRegistrar::getHead();
            heads._pEnum      = EnumRegistrar::getHead();
            heads._pVariable  = GlobalVariableRegistrar::getHead();
            heads._pDataKind  = AssetDataKindRegistrar::getHead();
            heads._pObject    = ModuleObjectRegistrar::getHead();
            heads._pSubsystem = ModuleSubsystemRegistrar::getHead();
            return heads;
        }

        void setGlobalRegistrarHeads( const ModuleRegistrarHeads& heads )
        {
            TypeRegistrar::getHead()            = heads._pType;
            EnumRegistrar::getHead()            = heads._pEnum;
            GlobalVariableRegistrar::getHead()  = heads._pVariable;
            AssetDataKindRegistrar::getHead()   = heads._pDataKind;
            ModuleObjectRegistrar::getHead()    = heads._pObject;
            ModuleSubsystemRegistrar::getHead() = heads._pSubsystem;
        }

        void registerModuleTypes( string_view moduleName, const ModuleRegistrarHeads& heads )
        {
            SW_MEMORY_SCOPE( Reflection );
            TypeRegistrar* pTypeHead = heads._pType;
            EnumRegistrar* pEnumHead = heads._pEnum;
            // 전역 변수는 새로 뗀 것만 올린다(아래 캐시에 넣지 않는다) — 각자 찍힌 모듈 이름으로. 모듈이 자기 변수를 읽기 전에 커맨드라인 보류값이 여기서 적용된다.
            if ( heads._pVariable != nullptr )
                getSubsystem<GlobalVariableManager>().registerPendingVariables( heads._pVariable );
            // 데이터 파일 종류도 새로 뗀 것만 올린다. 모듈을 내리기 전에 같은 이름으로 뺀다(`unregisterModuleTypes`).
            if ( heads._pDataKind != nullptr )
                (void)getSubsystem<AssetDataKindRegistry>().registerModuleKinds( heads._pDataKind );
            // 엔진 밖 레지스트리(편집기 확장 · 키트)의 등록자도 새로 뗀 것만 붙인다. 모듈을 내리기 전에 같은 이름으로 뗀다.
            if ( heads._pObject != nullptr )
                attachModuleObjects( moduleName, heads._pObject );

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
            // 등록은 모든 타입의 상속 캐시를 비운다 — 워커가 짓는 씬 로드가 그 캐시를 읽는 중이면 경합이다. 먼저 끝나기를 기다린다.
            getSubsystem<SceneManager>().waitForPendingLoad();
            getSubsystem<TypeRegistry>().registerPendingTypes( pTypeHead, pEnumHead );

            // 모듈 서브시스템은 타입이 오른 뒤에 선다(초기화가 그 모듈의 리플렉션 타입을 쓸 수 있다). 새로 뗀 것만 — 리로드는 내릴 때 걷었다.
            if ( heads._pSubsystem != nullptr )
            {
                (void)getRegistrationHost().mountModule( moduleName, heads._pSubsystem ); // 실패는 오류로 남았다 — 그 서브시스템을 찾는 쪽이 없다는 오류로 멈춘다
            }

            for ( const auto& scene : getSubsystem<SceneManager>().getLoadedScenes() )
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
            GlobalVariableManager& variables = getSubsystem<GlobalVariableManager>();
            variables.setEnumTextParser( SW_DELEGATE_FUNCTION( GlobalVariableEnumTextParser, parseGlobalVariableEnumText ) );
            return variables.applyPendingEnumText();
        }

        void unbindGlobalVariableEnumNames()
        {
            // 파서는 매니저의 멤버다 — 매니저가 이미 내려갔으면 뗄 것도 함께 사라졌다.
            GlobalVariableManager* const pVariables = findSubsystem<GlobalVariableManager>();
            if ( pVariables != nullptr )
                pVariables->setEnumTextParser( GlobalVariableEnumTextParser{} );
        }

#if !defined( SW_SHIPPING )
        void unregisterModuleTypes( string_view moduleName )
        {
            getModuleHeadCache().erase( string{ moduleName } );
            // 모듈 타입의 인스턴스를 든 쪽(UI 화면 · 어빌리티 · 편집기)이 타입의 모듈 이름으로 자기 것을 놓는다 — 타입과 코드가 아직 있을 때다.
            getSubsystem<TypeRegistry>().publishModuleUnloading( moduleName );
            // 그다음 모듈의 등록 줄을 뗀다 — 레지스트리가 그 줄로 만든 인스턴스(패널 · 명령 …)를 이 안에서 거둔다.
            detachModuleObjects( moduleName );
            getSubsystem<SceneManager>().waitForPendingLoad(); // 해제도 상속 캐시를 비운다(등록과 같은 이유)
            // 씬은 엔진이 소유해 모듈보다 오래 산다. 이 모듈 타입의 인스턴스가 남아 있으면 vtable 이 사라진 객체가 된다.
            // 타입(생성 함수 포함)을 걷기 **전에** 인스턴스부터 지운다(소멸자가 아직 있는 동안).
            for ( const auto& scene : getSubsystem<SceneManager>().getLoadedScenes() )
            {
                if ( scene == nullptr || scene->getObjectManager() == nullptr )
                    continue;
                // 남의 모듈 타입 객체를 든 컴포넌트(어빌리티 · 세트)가 먼저 놓고, 그 모듈 타입의 컴포넌트를 지운다.
                scene->getObjectManager()->forEachComponent( [moduleName]( Component* pComponent )
                {
                    pComponent->onModuleUnloading( moduleName );
                } );
                scene->getObjectManager()->destroyComponentsOfModule( moduleName );
            }
            // 모듈 서브시스템은 그것을 빌려 쓰는 인스턴스(컴포넌트 · 등록 줄)가 모두 놓은 뒤, 타입과 코드가 아직 있을 때 걷는다.
            getRegistrationHost().unmountModule( moduleName );
            getSubsystem<TypeRegistry>().unregisterTypesByModule( moduleName );
            getSubsystem<GlobalVariableManager>().unregisterVariablesByModule( moduleName );
            (void)getSubsystem<AssetDataKindRegistry>().unregisterModuleKinds( moduleName );
        }
#endif
    } // namespace engine
} // namespace sw
