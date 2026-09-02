/**
 * @file SchemaMigrate.h
 * @brief 스키마 버전 마이그레이션 컨텍스트 / orphan 필드 / 구조 이동 헬퍼
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
    struct TypeInfo;
    struct PropertyInfo;

    // ------------------------------------------------------------------------------
    // 1) SchemaOrphanValue — 현재 TypeInfo에 매칭·적용 실패한 와이어 필드
    // ------------------------------------------------------------------------------
    struct SchemaOrphanValue
    {
        hashed_string _name;
        uint32        _nameHash{ 0 };
        uint32        _wireTypeHash{ 0 }; ///< Binary 프로퍼티 타입 해시 (없으면 0)
        vector<uint8> _listBinary;
        string        _text;
    };

    // ------------------------------------------------------------------------------
    // 2) 스테이징 인스턴스 — legacyTypeInfo 버퍼 생성/파괴
    //    $ctor가 있으면 placement-new + _destroyInstance, 없으면 멤버별
    // ------------------------------------------------------------------------------
    /**
     * @brief legacyTypeInfo 스테이징 버퍼를 만듭니다.
     * @details `$ctor`가 있으면 전체 객체를 placement-new 하고 `_destroyInstance`로 대응 파괴합니다.
     *          없으면 string·hashed_string·컨테이너만 멤버별로 생성/파괴합니다.
     */
    SW_API void* createScratchInstance( const TypeInfo& typeInfo, vector<uint8>& listStorage );
    /** @brief 스테이징 인스턴스를 파괴합니다. */
    SW_API void destroyScratchInstance( void* pInstance, const TypeInfo& typeInfo );

    /**
     * @brief deserializeVersioned가 migrate를 호출할 때 전달.
     * @details 호출 조건: migrate != nullptr 이고 (버전 불일치 | orphans | legacyTypeInfo).
     *          migrate가 nullptr인데 버전 불일치나 orphan이 있으면 deserializeVersioned는 false.
     *          legacyInstance가 있으면 옛 TypeInfo로 스테이징한 뒤 현재 instance로 옮긴다.
     */
    struct SW_API SchemaMigrateContext
    {
        uint32                           _fromVersion{ 0 };
        uint32                           _toVersion{ 0 };
        void*                            _pInstance{ nullptr };
        const TypeInfo*                  _pTypeInfo{ nullptr };
        void*                            _pLegacyInstance{ nullptr };
        const TypeInfo*                  _pLegacyTypeInfo{ nullptr };
        const vector<SchemaOrphanValue>* _pOrphans{ nullptr };
        const SerializeContext*          _pSerializeCtx{ nullptr };

        // ------------------------------------------------------------------------------
        // 3) orphan 조회 · 현재 인스턴스에 적용
        // ------------------------------------------------------------------------------
        /** @brief 이름으로 orphan을 찾습니다. */
        const SchemaOrphanValue* findOrphan( hashed_string name ) const;
        /** @brief 이름 해시로 orphan을 찾습니다. */
        const SchemaOrphanValue* findOrphanHash( uint32 nameHash ) const;

        /** @brief orphan text/binary → 현재 인스턴스 프로퍼티 (wireTypeHint로 binary 해석). */
        bool applyOrphanTo( hashed_string propName, hashed_string wireTypeHint = {} ) const;

        /** @brief dotted path (`_stats._hp`)로 orphan을 적용합니다. */
        bool applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint = {} ) const;

        // ------------------------------------------------------------------------------
        // 4) 구조 이동 — legacy 프로퍼티 → 현재, 텍스트 강제 설정
        // ------------------------------------------------------------------------------
        /** @brief legacyInstance flat 프로퍼티 → 현재 프로퍼티 (텍스트 coerce). */
        bool moveProperty( hashed_string fromProp, hashed_string toProp ) const;

        /** @brief dotted path 구조 이동 (`_hp` → `_stats._hp`). */
        bool movePropertyPath( const utf8* pFromPath, const utf8* pToPath ) const;

        /** @brief 텍스트로 현재 인스턴스 프로퍼티를 설정합니다. */
        bool setPropertyFromText( hashed_string propName, string_view text ) const;
    };

    /** @brief fromVersion → toVersion. false면 deserializeVersioned 실패. */
    using SchemaMigrateFn = bool ( * )( const SchemaMigrateContext& ctx );

    /**
     * @brief deserializeVersioned 의 마지막 단계 — migrate 호출 조건 판정 + 실행, 또는 no-migrate 경고.
     * @details soft 역직렬화가 끝난 뒤 Json/Binary 가 공유하는 로직. 스크래치 인스턴스 파괴는 호출자 책임.
     * @param bWarnWhenNoMigrate migrate 가 없고 이 값이 true 이면 경고 후 false 반환(버전/orphan 정책은 호출자가 계산).
     */
    SW_API bool runSchemaMigrateStep( uint32 fromVersion, uint32 currentVersion, void* pInstance, const TypeInfo& typeInfo,
                                      void* pLegacyInstance, const TypeInfo* pLegacyTypeInfo,
                                      const vector<SchemaOrphanValue>& listOrphan, SchemaMigrateFn migrate,
                                      bool bWarnWhenNoMigrate, const SerializeContext& ctx );

    /** @brief Json/Xml 루트에 기록하는 스키마 버전 키. */
    inline constexpr auto kSchemaVersionKey = "_schemaVersion";
    /** @brief 스키마 마이그레이션 orphan 이 가리키는 PROPERTY 이름. */
    inline constexpr auto kPropertyNameKey     = "_name";
    inline constexpr auto kXmlPropertyNameAttr = kPropertyNameKey;
    /** @brief JSON 시퀀스 페이로드 키 (`"item": [...]`). */
    inline constexpr auto kJsonContainerItemKey = "item";
    /** @brief JSON 맵 페이로드 키 (`"entry": { ... }`). */
    inline constexpr auto kJsonContainerEntryKey = "entry";

    /** @brief XML 시퀀스 원소 태그. 구조체 원소는 대신 타입 이름을 씁니다. */
    inline constexpr auto kXmlItemTag = "item";
    /** @brief XML 맵 항목 태그와 키 속성 (`<entry key="a">`). */
    inline constexpr auto kXmlEntryTag = "entry";
    inline constexpr auto kXmlKeyAttr  = "key";

    // ------------------------------------------------------------------------------
    // 5) coerce · 경로 해석 — 바이너리/텍스트 강제 변환, dotted path
    // ------------------------------------------------------------------------------
    /**
     * @brief 바이너리 페이로드를 대상 타입으로 강제 변환 시도 (int32↔string 등).
     * @return 적용 성공 시 true.
     */
    SW_API bool tryCoerceBinaryPayload( void* pPropPtr, hashed_string targetTypeName,
                                        const uint8* pPayload, size_t payloadSize, const SerializeContext& ctx );

    /**
     * @brief 텍스트 토큰을 대상 타입으로 파싱 (따옴표 제거·numeric↔string coerce).
     */
    SW_API bool parseTextValueCoerced( void* pValPtr, hashed_string typeName, string_view valStr,
                                       const SerializeContext& ctx );

    /** @brief dotted path로 프로퍼티 포인터를 해석합니다. */
    SW_API bool resolvePropertyPath( void* pRoot, const TypeInfo& typeInfo, const utf8* pDottedPath,
                                     void*& pOutPtr, const PropertyInfo*& pOutProp );

} // namespace sw
