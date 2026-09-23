/**
 * @file SchemaMigrate.h
 * @brief 스키마 버전 이관(마이그레이션) 컨텍스트, orphan 필드, 구조 이동 도우미입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class SerializeContext;

    // ------------------------------------------------------------------------------
    // 1) SchemaOrphanValue: 현재 TypeInfo 에 맞는 자리가 없거나 적용하지 못한 기록 필드
    // ------------------------------------------------------------------------------
    struct SchemaOrphanValue
    {
        hashed_string _name;
        uint32        _nameHash{ 0 };
        uint32        _wireTypeHash{ 0 }; ///< 바이너리에 적힌 프로퍼티 타입 해시(없으면 0)
        vector<uint8> _listBinary;
        string        _text;
    };

    // ------------------------------------------------------------------------------
    // 2) 스테이징 인스턴스: legacyTypeInfo 버퍼 생성 · 파괴
    //    $ctor 가 있으면 placement new + _destroyInstance, 없으면 멤버별로
    // ------------------------------------------------------------------------------
    /**
     * @brief legacyTypeInfo 스테이징 버퍼를 만듭니다.
     * @details `$ctor` 가 있으면 객체 전체를 placement new 로 만들고 `_destroyInstance` 로 짝을 맞춰 파괴합니다.
     *          없으면 0 으로 채운 버퍼에서 멤버별로 만듭니다. 컨테이너 · string · hashed_string · atomic<bool> · TagID 와
     *          `$ctor` 가 있는 중첩 REFLECT 타입만 생성 · 파괴하고, 나머지는 0 인 채로 둡니다.
     */
    SW_API void* createScratchInstance( const TypeInfo& typeInfo, vector<uint8>& listStorage );
    /** @brief 스테이징 인스턴스를 파괴합니다. */
    SW_API void destroyScratchInstance( void* pInstance, const TypeInfo& typeInfo );

    /**
     * @brief deserializeVersioned 가 migrate 를 부를 때 넘기는 컨텍스트입니다.
     * @details 부르는 조건: migrate != nullptr 이고 (버전이 다름 | orphan 있음 | legacyTypeInfo 있음).
     *          migrate 가 nullptr 이면 버전이 다를 때 deserializeVersioned 는 false 를 반환합니다. 버전이 같고
     *          orphan 만 있을 때는 포맷마다 다릅니다. Binary 는 false, JSON · XML 은 orphan 을 버리고 성공합니다
     *          (`SchemaOrphanPolicy`). legacyTypeInfo 를 주면 같은 데이터를 옛 TypeInfo 로도 읽어
     *          `_pLegacyInstance` 에 스테이징합니다. 현재 instance 로 옮기는 것은 migrate 의 일입니다(`moveProperty` 등).
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
        /** @brief 이름으로 orphan 을 찾습니다. */
        const SchemaOrphanValue* findOrphan( hashed_string name ) const;
        /** @brief 이름 해시로 orphan 을 찾습니다. */
        const SchemaOrphanValue* findOrphanHash( uint32 nameHash ) const;

        /**
         * @brief orphan 의 값(텍스트 또는 바이너리)을 현재 인스턴스의 프로퍼티에 적용합니다.
         *        바이너리는 wireTypeHint(비우면 orphan 에 적힌 기록 타입)로 해석합니다.
         * @warning 기록 타입이 프로퍼티 타입과 다르면 지금 구현은 그 타입의 값을 프로퍼티 자리에 **먼저 씁니다.**
         *          모양이 다른 타입(int32 → string 등)이면 그 자리를 망가뜨립니다. `applyOrphanToPath` 도
         *          wireTypeHint 를 프로퍼티와 다른 타입으로 주면 같습니다(백로그 1-0g).
         */
        bool applyOrphanTo( hashed_string propName, hashed_string wireTypeHint = {} ) const;

        /** @brief 점으로 이은 경로(`_stats._hp`)로 orphan 을 적용합니다. */
        bool applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint = {} ) const;

        // ------------------------------------------------------------------------------
        // 4) 구조 이동: 옛 프로퍼티 → 현재 프로퍼티, 텍스트로 강제 설정
        // ------------------------------------------------------------------------------
        /** @brief legacyInstance(없으면 현재 인스턴스)의 프로퍼티 값을 현재 프로퍼티로 옮깁니다(텍스트를 거쳐 변환합니다). */
        bool moveProperty( hashed_string fromProp, hashed_string toProp ) const;

        /** @brief 점 경로로 구조를 옮깁니다(`_hp` → `_stats._hp`). */
        bool movePropertyPath( const utf8* pFromPath, const utf8* pToPath ) const;

        /** @brief 텍스트로 현재 인스턴스 프로퍼티를 설정합니다. */
        bool setPropertyFromText( hashed_string propName, string_view text ) const;
    };

    /** @brief fromVersion 에서 toVersion 으로 옮기는 이관 함수입니다. false 를 반환하면 deserializeVersioned 가 실패합니다. */
    using SchemaMigrateFn = bool ( * )( const SchemaMigrateContext& ctx );

    /**
     * @brief deserializeVersioned 의 마지막 단계입니다. migrate 를 부를 조건을 판정해 부르거나, migrate 가 없으면 경고합니다.
     * @details soft 역직렬화가 끝난 뒤의 공통 로직입니다. 세 포맷 모두 `runVersionedDeserialize` 를 거쳐 여기로 옵니다.
     *          스크래치 인스턴스 파괴는 부르는 쪽 책임입니다.
     * @param bWarnWhenNoMigrate migrate 가 없고 이 값이 true 이면 경고한 뒤 false 를 반환합니다(버전 · orphan 정책은 부르는 쪽이 계산합니다).
     */
    SW_API bool runSchemaMigrateStep( uint32 fromVersion, uint32 currentVersion, void* pInstance, const TypeInfo& typeInfo,
                                      void* pLegacyInstance, const TypeInfo* pLegacyTypeInfo,
                                      const vector<SchemaOrphanValue>& listOrphan, SchemaMigrateFn migrate,
                                      bool bWarnWhenNoMigrate, const SerializeContext& ctx );

    /** @brief Json/Xml 루트에 기록하는 스키마 버전 키입니다. */
    inline constexpr auto kSchemaVersionKey = "_schemaVersion";
    /** @brief 요소가 PROPERTY 이름을 속성으로 들 때의 속성 이름입니다. XML orphan 수집은 루트의 자식 요소가 이 속성으로 아는 PROPERTY 를 가리키면 orphan 으로 보지 않습니다. */
    inline constexpr auto kPropertyNameKey     = "_name";
    inline constexpr auto kXmlPropertyNameAttr = kPropertyNameKey;
    /** @brief 옛 JSON 래핑 표기의 시퀀스 키(`"item": [...]`)입니다. **지금은 아무도 쓰지 않습니다**(래핑 읽기는 `535181b4` 에서 지웠습니다). */
    inline constexpr auto kJsonContainerItemKey = "item";
    /** @brief 옛 JSON 래핑 표기의 맵 키(`"entry": { ... }`)입니다. **지금은 아무도 쓰지 않습니다**(래핑 읽기는 `535181b4` 에서 지웠습니다). */
    inline constexpr auto kJsonContainerEntryKey = "entry";

    /** @brief XML 시퀀스 원소 태그입니다. 구조체 원소는 대신 타입 이름을 태그로 씁니다. */
    inline constexpr auto kXmlItemTag = "item";
    /** @brief XML 맵 항목 태그와 키 속성입니다(`<entry key="a">`). */
    inline constexpr auto kXmlEntryTag = "entry";
    inline constexpr auto kXmlKeyAttr  = "key";

    // ------------------------------------------------------------------------------
    // 5) 강제 변환 · 경로 해석: 바이너리/텍스트 강제 변환, 점 경로
    // ------------------------------------------------------------------------------
    /**
     * @brief 바이너리 페이로드를 대상 타입으로 강제 변환해 봅니다(int32↔string 등).
     * @param wireTypeName 그 payload 를 **쓸 때의** 타입입니다. 알면 넘기십시오. POD 를 문자열로 바꿀 때
     *                     크기만으로는 정수와 실수를 가를 수 없어서(`sizeof(float32) == sizeof(int32)`)
     *                     `1.5f` 가 비트값 `"1069547520"` 이 됩니다. 비워 두면 크기로 짐작합니다.
     * @return 적용에 성공하면 true 입니다.
     */
    SW_API bool tryCoerceBinaryPayload( void* pPropPtr, hashed_string targetTypeName,
                                        const uint8* pPayload, size_t payloadSize,
                                        const SerializeContext& ctx,
                                        hashed_string           wireTypeName = hashed_string{} );

    /**
     * @brief 텍스트 토큰을 대상 타입으로 파싱합니다(따옴표 제거 · 숫자↔문자열 강제 변환).
     */
    SW_API bool parseTextValueCoerced( void* pValPtr, hashed_string typeName, string_view valStr,
                                       const SerializeContext& ctx );

    /** @brief 점 경로로 프로퍼티 포인터를 찾습니다. */
    SW_API bool resolvePropertyPath( void* pRoot, const TypeInfo& typeInfo, const utf8* pDottedPath,
                                     void*& pOutPtr, const PropertyInfo*& pOutProp );

} // namespace sw
