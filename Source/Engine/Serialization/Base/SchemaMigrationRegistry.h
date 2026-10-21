/**
 * @file SchemaMigrationRegistry.h
 * @brief 스키마 이관 함수의 레지스트리입니다. 타입 옆 .cpp 에 `SW_SCHEMA_MIGRATION` 한 줄로 (타입, 기록된 버전)마다 등록하고, 버전 붙은 읽기가 스스로 찾습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @struct SchemaMigration
     * @brief 등록 항목 하나입니다. 기록된 버전이 `_fromVersion` 인 `_pTypeName` 데이터를 지금 타입으로 옮기는 함수입니다.
     * @details 스키마가 바뀌면 기본은 데이터를 다시 쓰는 것이다(엔진 데이터에는 별칭 · 옛 형식 리더를 두지 않는다, 결정 기록 5-4).
     *          이관은 값 계산이 불가피한 예외(필드 나누기 · 합치기 · 단위 바꾸기)에만 쓰며 이름 별칭 표가 아니다. 같은 버전에서 지운 필드를
     *          버려도 되는지는 이관이 아니라 orphan 정책이고, 호출부가 컨텍스트로 정한다(`SerializeContext::setAllowUnknownProperties`).
     */
    struct SchemaMigration
    {
        const utf8*     _pTypeName;   ///< 타입의 FQN(`TypeInfo::_fullyQualifiedName`)입니다. 등록한 이미지의 정적 저장소에 있습니다.
        uint32          _fromVersion; ///< 데이터에 기록된 버전입니다.
        SchemaMigrateFn _migrate;     ///< 이관 함수입니다. nullptr 은 등록하지 않습니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class SchemaMigrationRegistry
     * @brief (타입 FQN, 기록된 버전) → 이관 함수 표입니다. 세 형식의 버전 붙은 읽기(`runSchemaMigrateStep`)가 여기서 찾으므로 호출부는 이관 함수를 넘기지 않습니다.
     * @details 이관 함수를 호출부에 넘기던 API 의 연결 방식만 바꾼 것이다. 호출부마다 함수 포인터를 넘기면 하나를 빠뜨려도 컴파일되므로,
     *          타입 옆에 한 번 등록하고 읽기가 찾는다. 언리얼의 커스텀 버전 등록(`FCustomVersionRegistration` 정적 등록자)과 같은 모양이다.
     *          모듈을 언로드하면 그 모듈의 정적 등록자가 소멸하며 빠지고, 핫 리로드의 교체 이미지는 같은 (타입, 버전)을 대체한다
     *          (`RegistrationReloadScope`). 찾기는 작업 스레드의 읽기에서도 오므로 잠금 안에서 한다. 찾은 함수는 잠금 밖에서 부르므로
     *          모듈 언로드는 직렬화가 도는 동안 하지 않는다(`LiveReloadManager::drainTasksBeforeUnload` 가 먼저 비운다, 결정 기록 5-4).
     */
    class SW_API SchemaMigrationRegistry
    {
    public:
        /** @brief 등록합니다. 같은 (타입, 버전)이 이미 있거나 이름 · 함수가 비면 모든 구성에서 프로세스를 멈춥니다(핫 리로드의 교체 이미지는 대체합니다). */
        static void registerMigration( const SchemaMigration* pMigration );
        static void unregisterMigration( const SchemaMigration* pMigration );
        /**
         * @brief @p typeInfo 부터 부모로 올라가며 (FQN, @p fromVersion) 이 맞는 첫 이관 함수를 찾습니다. 없으면 nullptr 입니다.
         * @details 부모로 올라가는 이유는 파생 타입이 기반의 버전을 그대로 쓰기 때문이다(기반 타입 하나로 읽는 버전 붙은 데이터).
         *          파생이 자기 이관을 등록하면 그것이 먼저다.
         */
        static SchemaMigrateFn find( const TypeInfo& typeInfo, uint32 fromVersion );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_SCHEMA_MIGRATION` 이 두는 정적 등록자입니다. 테스트는 함수 범위에 두어 범위가 끝나면 빼게 합니다. */
    struct SW_API SchemaMigrationRegistrar
    {
        SchemaMigrationRegistrar( const utf8* pTypeName, uint32 fromVersion, SchemaMigrateFn migrate );
        ~SchemaMigrationRegistrar();
        SchemaMigrationRegistrar( const SchemaMigrationRegistrar& )            = delete;
        SchemaMigrationRegistrar& operator=( const SchemaMigrationRegistrar& ) = delete;

        SchemaMigration _migration;
    };
} // namespace sw

/**
 * @brief 이관 함수를 등록합니다(.cpp 의 파일 범위). @p Type 은 전역 범위부터 쓴 이름(`sw::GameObject`)이어야 합니다.
 * @details 이름을 `::Type` 으로 한 번 더 써서 컴파일러가 확인하므로, 줄여 쓴 이름(`GameObject`)은 등록 FQN 과 어긋나기 전에 빌드가 멈춘다.
 */
#define SW_SCHEMA_MIGRATION( id, Type, fromVersion, migrate )                                    \
    static_assert( sizeof( ::Type ) > 0, "SW_SCHEMA_MIGRATION needs the fully qualified type" ); \
    static const ::sw::SchemaMigrationRegistrar sw_schemaMigration_##id                          \
    {                                                                                            \
        #Type, fromVersion, migrate                                                              \
    }
