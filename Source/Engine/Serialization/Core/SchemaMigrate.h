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
    /**
     * @enum BinaryWireVersion
     * @brief 바이너리 **값 인코딩**의 판입니다. 스트림마다 머리에 실립니다 — 태그 스트림은 프로퍼티 수(uint32)의 위 8비트, 컴팩트 스트림은 모드 바이트의 위 4비트.
     * @details 스트림마다 실리므로 그 안의 컨테이너 원소 · 맵 키도 같은 판으로 읽고, 중첩 구조체 · 소유 포인터 본문은 제 머리의 판을 읽습니다.
     *          세이브 · 플레이 스냅샷 · 쿠킹 씬 · 복사한 컴포넌트가 모두 같은 길입니다. 읽기는 지금 판 하나만 받습니다 — 판을 올리면 옛 판을 읽는 갈래를
     *          그때 더합니다. 0 은 enum 을 int64 값으로 싣던 판이라 쓰지 않습니다.
     */
    enum class BinaryWireVersion : uint8
    {
        EnumByName = 1, ///< enum 을 열거자 **이름 해시**로 싣는다(비트플래그는 켜진 이름들). 이름 없는 값 · 비트만 값으로 싣는다.
    };

    /** @brief 이 빌드가 쓰고 읽는 판입니다. 다른 판(앞선 빌드의 데이터 · 판이 없던 0)은 거절합니다. */
    inline constexpr BinaryWireVersion kCurrentBinaryWireVersion = BinaryWireVersion::EnumByName;

    struct SchemaOrphanValue
    {
        hashed_string     _name;
        uint32            _nameHash{ 0 };
        uint32            _wireTypeHash{ 0 };                        ///< 바이너리에 적힌 프로퍼티 타입 해시(없으면 0)
        BinaryWireVersion _wireVersion{ kCurrentBinaryWireVersion }; ///< 바이너리 값이 적힌 판 — 나중에 이관이 읽을 때 같은 판으로 읽는다
        /**
         * @brief 이관 함수가 이 값을 찾아 봤는지(`findOrphan` · `findOrphanHash` · `applyOrphanTo…`)입니다.
         * @details 찾아 본 값은 이관이 처리한 것이고, 아무도 찾지 않은 값은 로드가 **버린** 것입니다 — `runSchemaMigrateStep` 이
         *          버린 값을 로드마다 한 번 알립니다. 조회가 `const` 라 `mutable` 입니다.
         */
        mutable bool  _bClaimed{ false };
        vector<uint8> _listBinary;
        string        _text;
        /**
         * @brief 텍스트 형식에 적힌 이름 그대로입니다(타입이 모르는 키 · 태그 · 속성). 경고에 찍는 데만 씁니다.
         *        안쪽 원소(컴포넌트 · 구조체 칸)의 이름은 그 타입 이름이 앞에 붙습니다(`SpriteComponent._clipPth`).
         * @details 파일의 모르는 이름은 전역 이름 표에 넣지 않으므로 `_name` 이 비고 해시만 남습니다 — 그대로는 "어느 칸을 버렸나" 를
         *          말할 수 없습니다. 바이너리는 이름을 싣지 않아 늘 비어 있습니다.
         */
        string _writtenName;
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
     * @enum SchemaOrphanPolicy
     * @brief 버전은 같은데 **모르는 필드(orphan)만** 있을 때 migrate 없이 통과시킬지 정합니다. 포맷마다 답이 다르고, 그것이 계약입니다.
     *
     * @details - **텍스트(XML · JSON)는 `Ignore`** 입니다. 모르는 필드를 건너뛰고 아는 필드는 읽어 성공합니다. 손으로 고치고 버전 관리에서
     *            병합하는 저작 파일이라, 지운 PROPERTY 하나 · 오타 하나로 씬 · 프리팹 · 머티리얼이 통째로 안 읽히면 안 됩니다. 언리얼의 태그
     *            직렬화(`FPropertyTag` — 이름으로 찾고 모르는 태그는 크기만큼 건너뛴다)와 유니티 YAML(모르는 필드는 버린다)이 같은 규칙입니다.
     *            건너뛴 필드는 조용하지 않습니다 — `runSchemaMigrateStep` 이 타입 · 칸 이름과 함께 로드마다 한 번 경고합니다.
     *          - **바이너리는 `Reject`** 입니다. 쿠커 · 같은 빌드가 쓴 산출물이라, 모르는 필드가 있다는 것은 스키마가 바뀌었다는 뜻입니다. 이관
     *            함수(`SchemaMigrateFn`)가 받아 주지 않으면 실패합니다 — 언리얼 쿠킹 패키지가 판(`FPackageFileSummary` 의 버전)이 다르면 로드를
     *            거절하는 것과 같은 자리입니다. 오브젝트 상태처럼 지운 칸을 버려도 되는 자리는 이관 함수가 그렇게 말합니다
     *            (`ObjectStateSerializer` 의 `skipFieldsTheTypeNoLongerHas`).
     *          고정하는 시험: `ReflectionSerializationTest.OrphanOnlyPolicyDiffersByFormat`.
     */
    enum class SchemaOrphanPolicy : uint8
    {
        Ignore, ///< orphan 은 버리고(경고하고) 성공으로 봅니다(XML · JSON — 사람이 고치는 저작 파일).
        Reject  ///< orphan 이 있으면 migrate 없이는 실패합니다(바이너리 — 스키마가 바뀐 것이 확실합니다).
    };

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
        /** @brief 이름으로 orphan 을 찾습니다. 같은 이름의 orphan 은 모두 "이관이 처리했다"(`_bClaimed`)로 표시됩니다. */
        const SchemaOrphanValue* findOrphan( hashed_string name ) const;
        /** @brief 이름 해시로 orphan 을 찾습니다. 같은 해시의 orphan 은 모두 "이관이 처리했다"(`_bClaimed`)로 표시됩니다. */
        const SchemaOrphanValue* findOrphanHash( uint32 nameHash ) const;

        /**
         * @brief orphan 의 값(텍스트 또는 바이너리)을 현재 인스턴스의 프로퍼티에 적용합니다.
         * @details 바이너리는 wireTypeHint(비우면 orphan 에 적힌 기록 타입)로 해석합니다. 그 타입이 프로퍼티 타입과 같으면
         *          제자리로 읽고, 다르면 `tryCoerceBinaryPayload` 로 옮깁니다(본 역직렬화 경로와 같은 규칙입니다).
         */
        [[nodiscard]] bool applyOrphanTo( hashed_string propName, hashed_string wireTypeHint = {} ) const;

        /** @brief 점으로 이은 경로(`_stats._hp`)로 orphan 을 적용합니다. 바이너리 규칙은 `applyOrphanTo` 와 같습니다. */
        [[nodiscard]] bool applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint = {} ) const;

        // ------------------------------------------------------------------------------
        // 4) 구조 이동: 옛 프로퍼티 → 현재 프로퍼티, 텍스트로 강제 설정
        // ------------------------------------------------------------------------------
        /** @brief legacyInstance(없으면 현재 인스턴스)의 프로퍼티 값을 현재 프로퍼티로 옮깁니다(텍스트를 거쳐 변환합니다). */
        [[nodiscard]] bool moveProperty( hashed_string fromProp, hashed_string toProp ) const;

        /** @brief 점 경로로 구조를 옮깁니다(`_hp` → `_stats._hp`). */
        [[nodiscard]] bool movePropertyPath( const utf8* pFromPath, const utf8* pToPath ) const;

        /** @brief 텍스트로 현재 인스턴스 프로퍼티를 설정합니다. */
        bool setPropertyFromText( hashed_string propName, string_view text ) const;
    };

    /** @brief fromVersion 에서 toVersion 으로 옮기는 이관 함수입니다. false 를 반환하면 deserializeVersioned 가 실패합니다. */
    using SchemaMigrateFn = bool ( * )( const SchemaMigrateContext& ctx );

    /**
     * @brief deserializeVersioned 의 마지막 단계입니다. migrate 를 부를 조건을 판정해 부르거나, migrate 가 없으면 경고합니다.
     * @details soft 역직렬화가 끝난 뒤의 공통 로직입니다. 세 포맷 모두 `runVersionedDeserialize` 를 거쳐 여기로 옵니다.
     *          스크래치 인스턴스 파괴는 부르는 쪽 책임입니다.
     *          **orphan 의 운명이 정해지는 유일한 자리**이기도 합니다. 로드가 성공으로 끝나는데 migrate 가 찾아 보지 않은 orphan
     *          (읽지 못한 값 · 타입에 없는 칸)이 남으면 그 값은 버려진 것이므로, 타입 이름과 칸 이름을 담아 **로드마다 한 번** 경고합니다.
     *          JSON · XML 의 Ignore 정책도 여기서 알립니다(숫자 칸의 "abc" 가 말없이 기본값으로 남지 않게).
     *          옛 TypeInfo 를 스테이징했으면 두 타입 중 하나라도 아는 이름은 스테이징된 쪽이 실어 날랐다고 보고 빼고 셉니다.
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

    /** @brief XML 시퀀스 원소 태그입니다. 구조체 원소는 대신 타입 이름을 태그로 씁니다. */
    inline constexpr auto kXmlItemTag = "item";
    /** @brief XML 맵 항목 태그와 키 속성입니다(`<entry key="a">`). */
    inline constexpr auto kXmlEntryTag = "entry";
    inline constexpr auto kXmlKeyAttr  = "key";

    // ------------------------------------------------------------------------------
    // 5) 강제 변환 · 경로 해석: 바이너리/텍스트 강제 변환, 점 경로
    // ------------------------------------------------------------------------------
    /**
     * @brief 기록 타입과 대상 타입이 **값으로만** 옮기는 쌍인지 묻습니다 — 기록 타입을 아는 스칼라 → 다른 스칼라 · 문자열, 또는 기록 타입이 **다른** enum.
     * @details 이 쌍은 `tryCoerceBinaryPayload` 가 기록 값의 텍스트(스칼라는 수, enum 은 열거자 이름)를 대상 타입으로 다시 읽어 옮기고, 못 옮기면 실패입니다.
     *          부르는 쪽은 그 실패 뒤에 제 타입으로 다시 읽으면 안 됩니다. 크기가 같은 스칼라는 비트가 그대로 재해석되고, enum 의 이름 해시는 수로 읽힙니다.
     * @param wireTypeName 기록 타입. 비어 있으면(모름) false 입니다.
     */
    SW_API bool isValueOnlyCoercion( hashed_string targetTypeName, hashed_string wireTypeName );

    /**
     * @brief 바이너리 페이로드를 대상 타입으로 강제 변환해 봅니다(int32↔string 등).
     * @details 기록 타입을 아는 스칼라(정수 · 실수 · bool)를 다른 스칼라나 문자열로 바꿀 때는 **값으로** 옮깁니다
     *          (`isValueOnlyCoercion`). 기록 타입의 텍스트를 대상 타입으로 다시 읽으므로 JSON · XML 과 같은 규칙이고,
     *          float32 1.5 → int32 처럼 텍스트가 맞지 않으면 실패합니다. 기록 타입이 **다른 enum** 이면 그 enum 으로 읽어 열거자 이름(글)으로
     *          옮깁니다 — XML 이 이름을 적어 두는 것과 같은 결과다(문자열이면 이름, 같은 이름을 가진 enum 이면 그 열거자, 정수면 실패).
     *          그 밖에는 제 타입으로 끝까지 읽히는지부터 보고, 숫자 ↔ 문자열, 마지막으로 크기가 같은 POD 재해석을 시도합니다.
     * @param wireTypeName 그 payload 를 **쓸 때의** 타입입니다. 알면 넘기십시오(`findWireTypeName`). 비워 두면 크기로 짐작하는데, 크기만으로는
     *                     정수와 실수를 가를 수 없습니다(`sizeof(float32) == sizeof(int32)`).
     * @param wireVersion 그 payload 가 적힌 판입니다(스트림 머리 · orphan 이 든다).
     * @return 적용에 성공하면 true 입니다.
     */
    [[nodiscard]] SW_API bool tryCoerceBinaryPayload( void* pPropPtr, hashed_string targetTypeName,
                                                      const uint8* pPayload, size_t payloadSize,
                                                      const SerializeContext& ctx,
                                                      hashed_string           wireTypeName = hashed_string{},
                                                      BinaryWireVersion       wireVersion  = kCurrentBinaryWireVersion );

    /**
     * @brief 바이너리 태그의 기록 타입 해시를 이름으로 돌려줍니다 — 등록된 타입(정본 이름), 아니면 enum(FQN). 모르면 빈 이름입니다.
     * @details 타입 표(`canonicalTypeNameByHash`)는 enum 을 모르므로 enum 은 여기서 찾습니다. 기록 타입을 몰라 크기로 짐작하면 열거자 이름
     *          해시(4 바이트)가 int32 · float32 로 그대로 읽힌다.
     */
    SW_API hashed_string findWireTypeName( uint32 wireTypeHash );

    /**
     * @brief 텍스트 토큰을 대상 타입으로 파싱합니다(따옴표 제거 · 숫자↔문자열 강제 변환).
     */
    [[nodiscard]] SW_API bool parseTextValueCoerced( void* pValPtr, hashed_string typeName, string_view valStr,
                                                     const SerializeContext& ctx );

    /** @brief 점 경로로 프로퍼티 포인터를 찾습니다. */
    SW_API bool resolvePropertyPath( void* pRoot, const TypeInfo& typeInfo, const utf8* pDottedPath,
                                     void*& pOutPtr, const PropertyInfo*& pOutProp );

} // namespace sw
