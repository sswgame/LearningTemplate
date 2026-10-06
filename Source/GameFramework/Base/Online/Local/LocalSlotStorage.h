/**
 * @file LocalSlotStorage.h
 * @brief 로컬 저장의 바닥 — 슬롯 이름 → 봉투 바이트를 실제로 두는 곳(파일 · 메모리 · SQLite)과, 요청 하나를 봉투 · 규칙과 함께 실행하는 도우미.
 * @details 앞(`MemoryLocalStore` · `ThreadedLocalStore`)은 언제 · 어느 스레드에서 실행할지만 정하고, 무엇을 할지는 `LocalStoreRequestUtil::execute` 하나다 —
 *          저장소마다 슬롯 규칙 · 상한 · 봉투가 갈리지 않는다. 바닥은 한 스레드에서만 불린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"
#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ILocalSlotStorage
     * @brief 슬롯 바닥입니다. 쓰기는 슬롯 하나 단위로 원자적이어야 한다(옛 것 아니면 새 것).
     */
    class SW_GF_API ILocalSlotStorage
    {
    public:
        ILocalSlotStorage()          = default;
        virtual ~ILocalSlotStorage() = default;

        ILocalSlotStorage( const ILocalSlotStorage& )            = delete;
        ILocalSlotStorage& operator=( const ILocalSlotStorage& ) = delete;

        virtual LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )     = 0;
        virtual LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) = 0;
        virtual LocalStoreResult eraseSlot( const string& slot )                                     = 0;
        /** @brief @p groupPrefix(빈 글 = 모두)로 시작하는 슬롯을 이름순으로 붙입니다. */
        virtual LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 맡은 요청 하나입니다(앞이 바닥 스레드로 넘긴다). */
    struct LocalStoreRequest
    {
        vector<uint8>          _bytes{};
        string                 _slot{};
        LocalStoreWriteOptions _options{};
        uint64                 _requestId{ 0 };
        LocalStoreOperation    _operation{ LocalStoreOperation::Read };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LocalStoreRequestUtil
     * @brief 요청 하나를 바닥에 실행합니다 — 이름 규칙 · 크기 상한 · 봉투 짓기/풀기.
     */
    struct SW_GF_API LocalStoreRequestUtil
    {
        /** @brief 바닥에 닿기 전에 거절할 요청이면(이름 · 상한) Invalid 완료를 만들고 true 입니다. */
        static bool                 makeInvalidCompletion( const LocalStoreRequest& request, LocalStoreCompletion& outCompletion );
        static LocalStoreCompletion execute( LocalStoreRequest& request, ILocalSlotStorage& storage, const LocalSealContext& sealContext );
        static LocalStoreCompletion makeCompletion( const LocalStoreRequest& request, LocalStoreResult result );
    };
} // namespace sw
