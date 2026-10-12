/**
 * @file ResourceDataKind.h
 * @brief 데이터 파일 종류(카탈로그 계약)와 그 레지스트리, 등록 줄 `SW_RESOURCE_DATA_KIND` 를 정의합니다.
 * @details `Resource/` 아래 데이터 파일 한 종류가 "어느 경로의 파일이고, 어떤 로더로 읽는가" 를 코드 옆 한 줄에 적습니다. 종류 표를 테스트(`ResourceDataSchemaTest`)나 에디터에
 *          손으로 두지 않습니다 — 새 카탈로그를 더하면 그 카탈로그의 .cpp 에 등록 줄을 두는 것으로 스키마 테스트와 카탈로그 편집기가 같이 알게 됩니다.
 *          언리얼의 `UDataTable` 행 구조체 · `UPrimaryDataAsset` 종류 목록, 유니티의 `ScriptableObject` 타입 목록에 해당합니다.
 *
 *          경로 규칙은 선언으로 적는다(접두 · 포함 · 접미, 대소문자 무시). 함수로 적으면 에디터가 규칙을 보일 수 없고 두 종류가 같은 파일에 맞는지 따질 수 없다.
 *          한 파일은 정확히 한 종류에만 맞아야 한다(`ResourceDataKindRegistry::collectMatches` 가 둘 이상이면 테스트가 진다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    /** @brief 리소스 id 의 데이터 파일을 실제 로더로 읽습니다. 모르는 키 · 열거자 · 참조는 로더가 경고를 남기고 false 를 반환합니다. */
    using ResourceDataLoadFunc = bool ( * )( const string& resourceID );

    /** @brief 데이터 파일 한 종류의 등록 줄입니다. 문자열은 등록한 이미지(모듈)의 정적 저장소에 있습니다. */
    struct SW_API ResourceDataKind
    {
        const utf8*          _pLabel;    ///< 종류 이름입니다(`items`). 레지스트리 안에서 유일하며 대소문자를 가리지 않습니다.
        const utf8*          _pPrefix;   ///< 리소스 id 가 이것으로 시작해야 합니다(`game/`). nullptr 이면 보지 않습니다.
        const utf8*          _pContains; ///< 리소스 id 가 이것을 담아야 합니다(`/data/appearance/`). nullptr 이면 보지 않습니다.
        const utf8*          _pSuffix;   ///< 리소스 id 가 이것으로 끝나야 합니다(`/data/items.xml`). nullptr 이면 보지 않습니다.
        ResourceDataLoadFunc _pLoad;

        /** @brief @p resourceID 가 세 규칙에 모두 맞으면 true 입니다(대소문자 무시). 규칙이 하나도 없으면 아무 파일에도 맞지 않습니다. */
        bool matches( string_view resourceID ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ResourceDataKindRegistry
     * @brief 데이터 파일 종류의 레지스트리입니다(Engine 이미지에 하나 있으며, 엔진 · 키트 · 게임 모듈이 등록하고 모듈을 내리면 그 종류가 빠집니다).
     * @details 등록 · 해제는 이미지 로더 스레드(정적 초기화)에서, 조회는 테스트 · 에디터에서 오므로 목록은 잠금 하나로 지킵니다. 종류의 로더는 잠금 밖에서 부릅니다
     *          (찾은 등록 줄은 그 모듈이 내려가기 전까지 삽니다). 프레임마다 도는 경로가 아닙니다.
     */
    class SW_API ResourceDataKindRegistry
    {
    public:
        /** @brief 프로세스에 하나인 레지스트리입니다(Engine 이미지의 함수 정적). */
        static ResourceDataKindRegistry& get();

        ResourceDataKindRegistry();

        /** @brief 종류를 올립니다. 이름이 비었거나 이미 있거나 로더가 없으면 false 입니다. */
        bool registerKind( const ResourceDataKind* pKind );
        /** @brief 종류를 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterKind( const ResourceDataKind* pKind );
        /** @brief 이름(대소문자 무시)으로 종류를 찾습니다. */
        const ResourceDataKind* findByLabel( string_view label ) const;
        /** @brief @p resourceID 에 맞는 종류를 모두 모읍니다. 정상이면 0 개(카탈로그가 아닌 파일)나 1 개입니다. */
        void collectMatches( string_view resourceID, vector<const ResourceDataKind*>& outListKind ) const;
        /** @brief 등록한 순서대로 모든 종류를 모읍니다. */
        void collectKinds( vector<const ResourceDataKind*>& outListKind ) const;
        /** @brief 등록된 종류 수입니다. */
        uint32 getCount() const;

    private:
        mutable mutex                            _mutex;        ///< 등록 목록을 지킨다(로더 실행은 잠금 밖)
        RegistrationList<const ResourceDataKind> _registration; ///< 이름은 대소문자를 무시하고, 이름 없는 등록은 거절한다
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_RESOURCE_DATA_KIND` 가 두는 정적 등록자입니다 — 만들 때 올리고 없앨 때(모듈을 내릴 때) 뺍니다. */
    struct SW_API ResourceDataKindRegistrar
    {
        explicit ResourceDataKindRegistrar( const ResourceDataKind* pKind );
        ~ResourceDataKindRegistrar();
        ResourceDataKindRegistrar( const ResourceDataKindRegistrar& )            = delete;
        ResourceDataKindRegistrar& operator=( const ResourceDataKindRegistrar& ) = delete;

        const ResourceDataKind* _pKind;
        bool                    _bRegistered;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ResourceDataKindUtil
     * @brief 등록 줄에 쓰는 로더 모양입니다.
     */
    struct ResourceDataKindUtil
    {
        /** @brief `XMLCatalog` 를 물려받은 카탈로그를 `loadFromResource` 로 읽는 로더입니다. 등록 줄에 `&ResourceDataKindUtil::loadCatalog<CropCatalog>` 로 씁니다. */
        template <typename TCatalog>
        [[nodiscard]] static bool loadCatalog( const string& resourceID )
        {
            TCatalog catalog;
            return catalog.loadFromResource( resourceID );
        }
    };
} // namespace sw

/**
 * @brief 데이터 파일 종류를 그 카탈로그의 .cpp 에서 등록합니다. 모든 구성(Shipping 포함)에 남습니다 — Shipping 은 모듈을 통째로 링크합니다(`sw_linkWholeArchive`).
 * @param varName  파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pLabel   종류 이름(리터럴)
 * @param pPrefix  리소스 id 접두(리터럴 또는 nullptr)
 * @param pContains 리소스 id 에 담길 글(리터럴 또는 nullptr)
 * @param pSuffix  리소스 id 접미(리터럴 또는 nullptr)
 * @param pLoad    `bool( const string& )` 로더
 */
#define SW_RESOURCE_DATA_KIND( varName, pLabel, pPrefix, pContains, pSuffix, pLoad )                                          \
    static const ::sw::ResourceDataKind          sw_resourceDataKind_##varName{ pLabel, pPrefix, pContains, pSuffix, pLoad }; \
    static const ::sw::ResourceDataKindRegistrar sw_resourceDataKindRegistrar_##varName { &sw_resourceDataKind_##varName }
