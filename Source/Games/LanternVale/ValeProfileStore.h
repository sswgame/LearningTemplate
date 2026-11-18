/**
 * @file ValeProfileStore.h
 * @brief 서버가 계정마다 진행(`ValeProfile`)을 쓰고 읽는 자리입니다 — 로컬 슬롯 저장소(`ILocalStore`) 위에 계정 id 하나가 슬롯 하나(`vale/a<id>`)입니다.
 * @details 저장소는 요청한 순서대로 끝내므로(`ILocalStore` 계약) 끊긴 접속의 저장 뒤에 같은 계정의 재접속 읽기를 맡기면 새 값을 읽는다.
 *          읽기가 `NotFound` 면 새 계정이고, 깨졌거나(`Corrupt`) 디스크 오류면 그 접속을 받지 않는다 — 빈 진행으로 대신 들이면 다음 저장이 옛 진행을 덮는다.
 *          쓰기는 계정마다 디스크에 있다고 아는 바이트(읽었거나 맡긴 것)와 견주어 같으면 맡기지 않는다(언리얼 Push Model 처럼 바뀐 것만) — 자동 저장이 그대로인 진행을 다시 쓰지 않는다.
 *          shortcut: 슬롯 저장소는 서버 프로세스 하나의 디스크다 — 서버 여럿이 계정을 나누면 온라인 저장소(`IServiceStore`)의 레코드로 옮긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStore.h"

#include "Games/LanternVale/ValeWorld.h"

namespace sw
{
    /** @brief 끝난 읽기 하나입니다. */
    struct ValeProfileLoad
    {
        ValeProfile _profile{};
        uint64      _accountID{ 0 };
        uint8       _bOk{ SW_FALSE };    ///< 읽었거나 새 계정이다. 거짓이면 그 접속을 받지 않는다
        uint8       _bFound{ SW_FALSE }; ///< 저장된 진행이 있었다(거짓이면 새 계정 — `_profile` 은 기본값)
    };

    /**
     * @class ValeProfileStore
     * @brief 저장소를 빌려 계정 진행을 맡기고 끝난 것을 꺼냅니다. 게임 스레드에서 씁니다.
     */
    class ValeProfileStore final
    {
    public:
        ValeProfileStore();

        /** @brief 저장소를 빌립니다. 이 객체보다 오래 살아야 합니다. */
        void bindStore( ILocalStore* pStore ) { _pStore = pStore; }
        bool isBound() const { return _pStore != nullptr; }

        void requestLoad( uint64 accountID );
        /** @brief 진행을 쓰도록 맡깁니다. 그 계정의 디스크 바이트(읽었거나 앞서 맡긴 것)와 같으면 맡기지 않고 false 입니다. */
        bool requestSave( uint64 accountID, const ValeProfile& profile );
        /**
         * @brief 계정의 세션이 끝났습니다 — 그 계정의 쓰기가 모두 끝나면 디스크 바이트 기억을 지웁니다(쓰는 중이 아니면 바로). 들고 있는 바이트가 접속 중인 계정 수를 따른다.
         * @details 다시 들어오면 읽기가 기억을 다시 채운다. 세션을 닫는 쪽이 마지막 `requestSave` 뒤에 부른다.
         */
        void releaseAccount( uint64 accountID );
        /** @brief 끝난 읽기를 @p outListLoad 뒤에 붙입니다. 쓰기 실패는 오류 로그로 알립니다. */
        void poll( vector<ValeProfileLoad>& outListLoad );

        /** @brief 맡긴 뒤 아직 끝나지 않은 일 수입니다. */
        int32  getPendingCount() const { return _pStore != nullptr ? _pStore->getPendingCount() : 0; }
        uint64 getSavedCount() const { return _savedCount; }
        /** @brief 디스크 바이트와 같아 맡기지 않은 쓰기 수입니다. */
        uint64 getSkippedSaveCount() const { return _skippedSaveCount; }
        /** @brief 디스크 바이트를 기억하는 계정 수입니다(감시 · 테스트). */
        size_t getKnownAccountCount() const { return _mapStoredBytes.size(); }

        /** @brief 계정 id 의 슬롯 이름입니다(`vale/a<id>` — 슬롯 이름 규칙 `[0-9a-z_.-]` 안). */
        static string makeSlotName( uint64 accountID );

    private:
        /** @brief 계정 @p accountID 의 쓰기가 아직 끝나지 않았으면 true 입니다. */
        bool hasPendingWrite( uint64 accountID ) const;

    private:
        unordered_map<uint64, uint64> _mapPendingRead;  ///< 읽기 요청 id → 계정 id
        unordered_map<uint64, uint64> _mapPendingWrite; ///< 쓰기 요청 id → 계정 id
        /// 계정 id → 디스크에 있다고 아는 진행 바이트(읽었거나 맡긴 것). 쓰기가 지면 지워 다음 저장이 다시 쓴다. 세션이 끝난 계정은 쓰기가 끝나면 지운다(`releaseAccount`).
        unordered_map<uint64, vector<uint8>> _mapStoredBytes;
        unordered_set<uint64>                _setReleasedAccount; ///< 세션이 끝났지만 쓰기가 남아 기억을 아직 지우지 않은 계정
        vector<LocalStoreCompletion>         _listCompletionScratch;
        ILocalStore*                         _pStore;
        uint64                               _savedCount;
        uint64                               _skippedSaveCount;
    };
} // namespace sw
