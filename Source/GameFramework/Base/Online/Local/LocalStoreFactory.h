/**
 * @file LocalStoreFactory.h
 * @brief 로컬 저장 설정으로 프런트를 만듭니다. "file" 과 "memory" 는 기반이 만들고, 그 밖(예: 키트 `GF_SQLStore` 의 "sqlite")은 그 키트가 등록한 백엔드를 씁니다.
 * @details 기반은 키트를 include 하지 못하므로 등록 함수를 둔다. 등록되지 않은 이름은 기동 오류다(다른 저장소로 바꾸지 않는다).
 *          "file" · "memory" 도 키트 백엔드와 같은 표(`RegistrationList`)에 올라 있어, 기반 이름을 덮는 등록은 이름 중복으로 거절된다(이름 검사를 손으로 두지 않는다).
 *          키트는 등록을 손으로 부르지 않고 `.cpp` 의 정적 `LocalStoreBackendRegistration` 하나로 둔다. 모듈이 로드되면 오르고,
 *          이미지를 내릴 때 그 정적 등록 객체(구독)가 만들기 함수를 지운다. 핫 리로드가 옛 이미지의
 *          언로드를 미뤄도(정적 소멸자가 새 이미지의 등록 뒤에 돈다) 옛 함수가 남거나 새 등록을 지우지 않는다.
 *          모든 저장소는 전용 스레드 하나(`ThreadedLocalStore`) 위에 선다. 자리는 `LocalStoreLocation`(메모리 · 사용자 데이터 폴더 아래 · 절대)이고, 바닥이 자기가 열 수 없는 종류면 오류다
 *          (파일 바닥에 메모리 자리, 메모리 바닥에 디스크 자리 - 다른 바닥으로 바꾸지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/File/UserDataPath.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ILocalSlotStorage;

    /** @brief 로컬 저장 설정(게임 데이터)입니다. */
    struct LocalStoreConfig
    {
        string             _backend;  ///< "file", "memory", 등록된 이름("sqlite")
        LocalStoreLocation _location; ///< 슬롯을 두는 자리 - 기본값을 두지 않는다(어디에 쓸지는 부르는 쪽이 고른다)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 백엔드를 만드는 함수입니다. 디스크 자리면 @p location 을 `resolveDiskPath( gameName )` 으로 풉니다.
     *        열 수 없는 종류(파일 바닥에 메모리 자리)거나 실패하면 nullptr 과 까닭을 돌려줍니다.
     */
    using LocalSlotStorageCreateSignature = unique_ptr<ILocalSlotStorage>( const LocalStoreLocation& location, string_view gameName, string& outError );
    using LocalSlotStorageCreateFunction  = LocalSlotStorageCreateSignature*;

    /** @brief 백엔드 하나입니다. 이름은 설정 `_backend` 에 쓰고(정적 문자열), 만들기 함수는 비어 있을 수 없습니다(등록 객체가 함수 참조로만 받습니다). */
    struct LocalStoreBackend
    {
        const utf8*                    _pName;
        LocalSlotStorageCreateFunction _createFunction;
    };
} // namespace sw

namespace sw
{
    class LocalStoreBackendRegistration;

    /**
     * @struct LocalStoreFactory
     * @brief 로컬 저장 공장 · 바닥 레지스트리입니다. 바닥 테이블은 잠금으로 지키므로 모듈 로더 스레드에서 등록해도 된다.
     */
    struct SW_GF_API LocalStoreFactory
    {
        friend class LocalStoreBackendRegistration;

        static constexpr const utf8* kFileBackendName   = "file";
        static constexpr const utf8* kMemoryBackendName = "memory";

        /** @brief 프런트를 만듭니다. 봉인 인터페이스(@p sealContext)는 빌려 쓰므로 프런트보다 오래 살아야 합니다. */
        static unique_ptr<ILocalStore> createLocalStore( string_view gameName, const LocalStoreConfig& settings, const LocalSealContext& sealContext,
                                                         string& outError );

    private:
        /** @brief 바닥을 올립니다. 기반 이름("file" · "memory")이거나 같은 이름이 이미 있으면 오류다(앞 것을 덮지 않는다). */
        static void registerBackend( const LocalStoreBackend& backend );
        /** @brief @p backend 로 올린 항목만 내립니다. 언로드 리스너가 이미 지웠거나 새 이미지의 같은 이름 항목은 건드리지 않는다. */
        static void unregisterBackend( const LocalStoreBackend& backend );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalStoreBackendRegistration
     * @brief 키트의 백엔드 하나를 모듈 수명 동안 팩토리에 등록하는 정적 객체입니다. 키트 `.cpp` 의 `namespace sw` 에 하나 둡니다.
     * @details 테스트처럼 범위 안에서만 등록하려면 지역 객체로 둡니다.
     */
    class LocalStoreBackendRegistration
    {
    public:
        /** @param pBackendName 설정 `_backend` 에 쓰는 이름(정적 문자열) */
        LocalStoreBackendRegistration( const utf8* pBackendName, LocalSlotStorageCreateSignature& createFunction )
            : _backend{ pBackendName, &createFunction }
        {
            LocalStoreFactory::registerBackend( _backend );
        }
        ~LocalStoreBackendRegistration() { LocalStoreFactory::unregisterBackend( _backend ); }

        LocalStoreBackendRegistration( const LocalStoreBackendRegistration& )            = delete;
        LocalStoreBackendRegistration& operator=( const LocalStoreBackendRegistration& ) = delete;

    private:
        LocalStoreBackend _backend;
    };
} // namespace sw
