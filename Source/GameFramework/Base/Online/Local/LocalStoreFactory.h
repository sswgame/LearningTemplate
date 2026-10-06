/**
 * @file LocalStoreFactory.h
 * @brief 로컬 저장 설정 → 앞입니다. "file" · "memory" 는 기반이 만들고, 그 밖(예: "sqlite" — 키트 `GF_SqlStore`)은 그 키트가 등록한 바닥을 씁니다.
 * @details 기반은 키트를 include 하지 못하므로 등록 창구를 둔다. 등록되지 않은 이름은 기동 오류다(다른 저장소로 바꾸지 않는다).
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
    struct LocalStoreSettings
    {
        string _backend{ "file" }; ///< "file" · "memory" · 등록된 이름("sqlite")
        string _root{ "saves" };   ///< 사용자 데이터 폴더 기준 상대 경로(절대 경로면 그대로)
    };

    /** @brief 바닥을 만드는 함수 — @p rootPath 는 이미 절대 경로다. 실패하면 nullptr 과 까닭. */
    using LocalSlotStorageCreateFunction = unique_ptr<ILocalSlotStorage> ( * )( string_view rootPath, string& outError );
} // namespace sw

namespace sw
{
    /**
     * @struct LocalStoreFactory
     * @brief 로컬 저장 공장 · 바닥 등록부입니다(게임 스레드).
     */
    struct SW_GF_API LocalStoreFactory
    {
        static constexpr const utf8* kFileBackendName   = "file";
        static constexpr const utf8* kMemoryBackendName = "memory";

        /** @brief 앞을 만듭니다. 봉인 창구(@p sealContext)는 빌려 쓴다 — 앞보다 오래 살아야 한다. */
        static unique_ptr<ILocalStore> createLocalStore( string_view gameName, const LocalStoreSettings& settings, const LocalSealContext& sealContext,
                                                         string& outError );
        /** @brief 바닥을 이름으로 올립니다. 기반 이름이거나 이미 있으면 false(앞 것을 덮지 않는다). 올린 모듈이 내려가기 전에 `unregisterBackend`. */
        [[nodiscard]] static bool registerBackend( string_view backendName, LocalSlotStorageCreateFunction createFunction );
        static void               unregisterBackend( string_view backendName );
    };
} // namespace sw
