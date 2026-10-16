#include "pch.h"

#include "Engine/Serialization/Base/SchemaMigrationRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Reflection/ReflectionCore.h"

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
    bool SchemaMigrationRegistry::registerMigration( const SchemaMigration* pMigration )
    {
        const bool bInvalidMigration = pMigration == nullptr || pMigration->_pTypeName == nullptr || pMigration->_migrate == nullptr;
        if ( bInvalidMigration )
            return false;
        const string                 key = SchemaMigrationRegistryInternal::makeKey( *pMigration );
        const std::lock_guard<mutex> lock( SchemaMigrationRegistryInternal::getMutex() );
        if ( SchemaMigrationRegistryInternal::getMigrations().add( pMigration, key ) != RegistrationResult::Added )
        {
            SW_LOG_ERROR( "Schema migration <%#> is already registered - the second one is ignored", key.c_str() );
            return false;
        }
        return true;
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
        , _bRegistered{ false }
    {
        _bRegistered = SchemaMigrationRegistry::registerMigration( &_migration );
    }

    SchemaMigrationRegistrar::~SchemaMigrationRegistrar()
    {
        if ( _bRegistered )
            SchemaMigrationRegistry::unregisterMigration( &_migration );
    }
} // namespace sw
