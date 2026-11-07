/**
 * @file ValeProfileStore.h
 * @brief 서버가 계정마다 진행(`ValeProfile`)을 쓰고 읽는 자리입니다 — 로컬 슬롯 저장소(`ILocalStore`) 위에 계정 id 하나가 슬롯 하나(`vale/a<id>`)입니다.
 * @details 저장소는 요청한 순서대로 끝내므로(`ILocalStore` 계약) 끊긴 접속의 저장 뒤에 같은 계정의 재접속 읽기를 맡기면 새 값을 읽는다.
 *          읽기가 `NotFound` 면 새 계정이고, 깨졌거나(`Corrupt`) 디스크 오류면 그 접속을 받지 않는다 — 빈 진행으로 대신 들이면 다음 저장이 옛 진행을 덮는다.
 *          shortcut: 슬롯 저장소는 서버 프로세스 하나의 디스크다 — 서버 여럿이 계정을 나누면 온라인 저장소(`IServiceStore`)의 레코드로 옮긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
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
        void requestSave( uint64 accountID, const ValeProfile& profile );
        /** @brief 끝난 읽기를 @p outListLoad 뒤에 붙입니다. 쓰기 실패는 오류 로그로 알립니다. */
        void poll( vector<ValeProfileLoad>& outListLoad );

        /** @brief 맡긴 뒤 아직 끝나지 않은 일 수입니다. */
        int32  getPendingCount() const { return _pStore != nullptr ? _pStore->getPendingCount() : 0; }
        uint64 getSavedCount() const { return _savedCount; }

        /** @brief 계정 id 의 슬롯 이름입니다(`vale/a<id>` — 슬롯 이름 규칙 `[0-9a-z_.-]` 안). */
        static string makeSlotName( uint64 accountID );

    private:
        unordered_map<uint64, uint64> _mapPendingRead;  ///< 읽기 요청 id → 계정 id
        unordered_map<uint64, uint64> _mapPendingWrite; ///< 쓰기 요청 id → 계정 id
        vector<LocalStoreCompletion>  _listCompletionScratch;
        ILocalStore*                  _pStore;
        uint64                        _savedCount;
    };
} // namespace sw
