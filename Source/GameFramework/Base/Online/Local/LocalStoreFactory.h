/**
 * @file LocalStoreFactory.h
 * @brief 로컬 저장 설정 → 앞입니다. "file" · "memory" 는 기반이 만들고, 그 밖(예: "sqlite" — 키트 `GF_SQLStore`)은 그 키트가 등록한 바닥을 씁니다.
 * @details 기반은 키트를 include 하지 못하므로 등록 창구를 둔다. 등록되지 않은 이름은 기동 오류다(다른 저장소로 바꾸지 않는다).
 *          키트는 등록을 손으로 부르지 않고 `.cpp` 의 정적 `LocalStoreBackendRegistration` 하나로 둔다. 모듈이 로드되면 오르고,
 *          언로드 직전에는 바닥 표가 모듈 언로드 리스너(`IModuleUnloadListener`)로서 그 이미지의 만들기 함수를 지운다. 핫 리로드가 옛 이미지의
 *          언로드를 미뤄도(정적 소멸자가 새 이미지의 등록 뒤에 돈다) 옛 함수가 남거나 새 등록을 지우지 않는다.
 *          모든 저장소는 전용 스레드 하나(`ThreadedLocalStore`) 위에 선다. 경로는 `UserDataPath::resolve( 게임 이름, _root )`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ILocalSlotStorage;

    /** @brief 로컬 저장 설정(게임 데이터)입니다. */
    struct LocalStoreConfig
    {
        string _backend{ "file" }; ///< "file" · "memory" · 등록된 이름("sqlite")
        string _root{ "saves" };   ///< 사용자 데이터 폴더 기준 상대 경로(절대 경로면 그대로)
    };

    /** @brief 바닥을 만드는 함수 — @p rootPath 는 이미 절대 경로다. 실패하면 nullptr 과 까닭. */
    using LocalSlotStorageCreateFunction = unique_ptr<ILocalSlotStorage> ( * )( string_view rootPath, string& outError );
} // namespace sw

namespace sw
{
    class LocalStoreBackendRegistration;

    /**
     * @struct LocalStoreFactory
     * @brief 로컬 저장 공장 · 바닥 레지스트리입니다. 바닥 표는 잠금으로 지키므로 모듈 로더 스레드에서 올려도 된다.
     */
    struct SW_GF_API LocalStoreFactory
    {
        friend class LocalStoreBackendRegistration;

        static constexpr const utf8* kFileBackendName   = "file";
        static constexpr const utf8* kMemoryBackendName = "memory";

        /** @brief 앞을 만듭니다. 봉인 창구(@p sealContext)는 빌려 쓴다 — 앞보다 오래 살아야 한다. */
        static unique_ptr<ILocalStore> createLocalStore( string_view gameName, const LocalStoreConfig& settings, const LocalSealContext& sealContext,
                                                         string& outError );

    private:
        /** @brief 바닥을 이름으로 올립니다. 기반 이름이거나 같은 이름이 이미 있으면 오류다(앞 것을 덮지 않는다). */
        static void registerBackend( const utf8* pBackendName, LocalSlotStorageCreateFunction createFunction );
        /** @brief @p createFunction 으로 올린 항목만 내립니다. 언로드 리스너가 이미 지웠거나 새 이미지의 같은 이름 항목은 건드리지 않는다. */
        static void unregisterBackend( LocalSlotStorageCreateFunction createFunction );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalStoreBackendRegistration
     * @brief 키트의 바닥 하나를 모듈 수명 동안 공장에 올리는 정적 객체입니다. 키트 `.cpp` 의 `namespace sw` 에 하나 둡니다.
     * @details 테스트처럼 범위 안에서만 올리려면 지역 객체로 둡니다.
     */
    class LocalStoreBackendRegistration
    {
    public:
        /** @param pBackendName 설정 `_backend` 에 쓰는 이름(정적 문자열) */
        LocalStoreBackendRegistration( const utf8* pBackendName, LocalSlotStorageCreateFunction createFunction )
            : _createFunction{ createFunction }
        {
            LocalStoreFactory::registerBackend( pBackendName, createFunction );
        }
        ~LocalStoreBackendRegistration() { LocalStoreFactory::unregisterBackend( _createFunction ); }

        LocalStoreBackendRegistration( const LocalStoreBackendRegistration& )            = delete;
        LocalStoreBackendRegistration& operator=( const LocalStoreBackendRegistration& ) = delete;

    private:
        LocalSlotStorageCreateFunction _createFunction;
    };
} // namespace sw
