#include "pch.h"

#include "Engine/Serialization/Base/SchemaMigrationRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Reflection/ReflectionCore.h"

#include <cstdio>
#include <cstdlib>

namespace sw
{
    SW_LOG_CALLER( "SchemaMigrationRegistry" );

    namespace
    {
        struct SchemaMigrationRegistryInternal
        {
            /** @brief 등록된 이관(등록 순서)입니다. 이름은 `<FQN>@<버전>` 이라 같은 (타입, 버전)을 거절하고 리로드가 대체합니다. Engine 이미지의 함수 정적입니다. */
            static RegistrationList<const SchemaMigration>& getMigrations()
            {
                static RegistrationList<const SchemaMigration> s_migration{ "SchemaMigrationRegistry", RegistrationOrder::Insertion, true };
                return s_migration;
            }

            /** @brief 등록과 찾기를 묶는 잠금입니다. 찾기는 작업 스레드의 읽기에서도 옵니다. */
            static mutex& getMutex()
            {
                static mutex s_mutex;
                return s_mutex;
            }

            static string makeKey( const SchemaMigration& migration )
            {
                return string( migration._pTypeName ) + "@" + to_string( migration._fromVersion );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SchemaMigrationRegistry::registerMigration( const SchemaMigration* pMigration )
    {
        const bool         bInvalidMigration = pMigration == nullptr || pMigration->_pTypeName == nullptr || pMigration->_migrate == nullptr;
        RegistrationResult result            = RegistrationResult::NullItem;
        string             key;
        if ( bInvalidMigration == false )
        {
            key = SchemaMigrationRegistryInternal::makeKey( *pMigration );
            const std::lock_guard<mutex> lock( SchemaMigrationRegistryInternal::getMutex() );
            result = SchemaMigrationRegistryInternal::getMigrations().add( pMigration, key );
        }
        if ( result == RegistrationResult::Added )
            return;
        // 오르지 못한 이관은 그 데이터를 읽을 때에야 드러나고, 어느 것이 쓰일지 정적 초기화 순서가 정한다. 고칠 곳은 등록 줄이므로 모든 구성에서 멈춘다.
        // 모듈 로드(정적 초기화) 중이라 로거가 아직 없을 수 있어 표준 오류에도 직접 쓴다(`OnlineProtocolRegistry` 와 같다).
        const utf8* pKey = key.empty() ? "(missing type name or function)" : key.c_str();
        std::fprintf( stderr, "Schema migration <%s> could not be registered (missing or duplicate)\n", pKey );
        std::fflush( stderr );
        SW_LOG_ERROR( "Schema migration <%#> could not be registered (missing or duplicate)", pKey );
        std::abort();
    }

    void SchemaMigrationRegistry::unregisterMigration( const SchemaMigration* pMigration )
    {
        const std::lock_guard<mutex> lock( SchemaMigrationRegistryInternal::getMutex() );
        (void)SchemaMigrationRegistryInternal::getMigrations().remove( pMigration ); // 등록되어 있지 않은 이관을 빼는 것은 할 일이 없다
    }

    SchemaMigrateFn SchemaMigrationRegistry::find( const TypeInfo& typeInfo, uint32 fromVersion )
    {
        // 부모 찾기(`getParentType`)는 타입 레지스트리를 잠글 수 있으므로 이 표의 잠금 밖에서 한다.
        for ( const TypeInfo* pType = &typeInfo; pType != nullptr; pType = pType->getParentType() )
        {
            const string_view            typeName{ pType->_fullyQualifiedName.c_str() };
            const std::lock_guard<mutex> lock( SchemaMigrationRegistryInternal::getMutex() );
            for ( const SchemaMigration* pMigration : SchemaMigrationRegistryInternal::getMigrations().getItems() )
            {
                const bool bMatches = pMigration->_fromVersion == fromVersion && typeName == string_view{ pMigration->_pTypeName };
                if ( bMatches )
                    return pMigration->_migrate;
            }
        }
        return nullptr;
    }

    void SchemaMigrationRegistry::releaseUnusedStorage()
    {
        const std::lock_guard<mutex> lock( SchemaMigrationRegistryInternal::getMutex() );
        SchemaMigrationRegistryInternal::getMigrations().shrinkToFit();
    }

    SchemaMigrationRegistrar::SchemaMigrationRegistrar( const utf8* pTypeName, uint32 fromVersion, SchemaMigrateFn migrate )
        : _migration{ pTypeName, fromVersion, migrate }
    {
        SchemaMigrationRegistry::registerMigration( &_migration );
    }

    SchemaMigrationRegistrar::~SchemaMigrationRegistrar()
    {
        SchemaMigrationRegistry::unregisterMigration( &_migration );
    }
} // namespace sw
