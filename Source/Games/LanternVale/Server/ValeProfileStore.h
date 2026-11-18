/**
 * @file ValeProfileStore.h
 * @brief 서버가 계정마다 진행(`ValeProfile`)을 쓰고 읽는 자리입니다 — 저장소 계약(`IServiceStore`) 위에 계정 id 하나가 레코드 하나(`vale_profile` 표, 키 `a<16자 id>`)입니다.
 * @details - 저장소는 전용 서버의 온라인 조립 저장소다. 맡길 때마다 찾는다(`ValeServiceStoreSource` — 조립은 리로드 · 종료마다 다시 서므로 들고 있지 않는다).
 *            서버 여럿이 같은 DB 를 쓰면 같은 레코드를 본다 — 쓰기는 읽었거나 마지막으로 쓴 판(`ServiceRecord::_version`)을 조건으로 걸어, 다른 서버가 그새 쓴 진행을 덮지 않는다.
 *          - 계정마다 쓰기는 하나씩만 맡긴다. 쓰는 중에 또 저장하면 가장 새 바이트만 들고 있다가 앞 쓰기가 끝나면 그 판으로 맡긴다(쓰기 합치기). 같은 계정의 읽기는
 *            남은 쓰기가 끝난 뒤에 맡긴다 — 끊긴 접속의 저장 뒤 재접속 읽기가 새 값을 읽는다.
 *          - 읽기가 없음(`NotFound`)이면 새 계정이고, 깨졌거나 저장소 오류면 그 접속을 받지 않는다 — 빈 진행으로 대신 들이면 다음 저장이 옛 진행을 덮는다.
 *          - 쓰기는 계정마다 저장소에 있다고 아는 바이트와 견주어 같으면 맡기지 않는다(언리얼 Push Model 처럼 바뀐 것만). 세션이 끝난 계정의 기억은 쓰기가 끝나면 지운다.
 *          - 일의 완료(`complete`)는 저장소를 거두는 쪽(온라인 조립)이 부른다. 일은 이 객체가 아니라 함께 드는 받은 편지함(`Inbox`)에 결과를 둔다 — 링크가 먼저 내려가도 일이 매달리지 않는다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include "Games/LanternVale/ValeWorld.h"

namespace sw
{
    /** @brief 진행 저장소를 찾는 곳입니다. 지금 저장소가 없으면(조립이 서 있지 않다) nullptr 을 돌려준다. */
    using ValeServiceStoreSource = Delegate<IServiceStore*()>;

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
     * @brief 저장소에 계정 진행 읽기 · 쓰기를 맡기고 끝난 것을 꺼냅니다. 게임 스레드(서버 루프)에서 씁니다. 파일 머리말 참고.
     */
    class ValeProfileStore final
    {
    public:
        /** @brief 진행 레코드의 표 이름입니다. */
        static constexpr const utf8* kTableName = "vale_profile";

        ValeProfileStore();

        /** @brief 저장소를 찾는 곳을 겁니다. 빈 것을 걸면 새 일을 맡기지 않습니다(맡긴 일은 저장소가 끝낸다). */
        void bindSource( ValeServiceStoreSource source ) { _source = std::move( source ); }
        bool isBound() const { return _source.isBound(); }

        /** @brief 계정의 진행 읽기를 맡깁니다. 그 계정의 쓰기가 남았으면 끝난 뒤에 맡깁니다. 저장소가 없으면 받지 않는 읽기 결과가 다음 `poll` 에 나온다. */
        void requestLoad( uint64 accountID );
        /**
         * @brief 진행을 쓰도록 맡깁니다. 그 계정이 저장소에 있다고 아는 바이트(쓰는 중이면 기다리는 바이트)와 같으면 맡기지 않고 false 입니다.
         * @details 읽은 적이 없는 계정(판을 모른다)은 쓰지 않고 오류를 남긴다 — 조건 없이 쓰면 다른 서버의 진행을 덮는다.
         */
        bool requestSave( uint64 accountID, const ValeProfile& profile );
        /** @brief 계정의 세션이 끝났습니다 — 그 계정의 쓰기가 모두 끝나면 기억(바이트 · 판)을 지웁니다(쓰는 중이 아니면 바로). */
        void releaseAccount( uint64 accountID );
        /** @brief 끝난 일을 거둬 끝난 읽기를 @p outListLoad 뒤에 붙입니다. 기다리던 쓰기 · 읽기를 이어 맡깁니다. 쓰기 결과는 일이 로그로 남겼다. */
        void poll( vector<ValeProfileLoad>& outListLoad );

        /** @brief 맡긴 뒤 아직 거두지 않은 일 수입니다. */
        int32  getPendingCount() const { return _pendingJobCount; }
        uint64 getSavedCount() const { return _savedCount; }
        /** @brief 저장소 바이트와 같아 맡기지 않은 쓰기 수입니다. */
        uint64 getSkippedSaveCount() const { return _skippedSaveCount; }
        /** @brief 기억(바이트 · 판)을 든 계정 수입니다(감시 · 테스트). */
        size_t getKnownAccountCount() const { return _mapAccount.size(); }

        /** @brief 계정 id 의 레코드 키입니다(`a` + 16자 16 진수 — 키 규칙 `[0-9a-z_./-]` 안). */
        static string makeRecordKey( uint64 accountID );
        /** @brief 레코드 바이트입니다 — 판(`ValeProfile::kFormatVersion`, 가변 정수) 뒤에 진행. */
        static vector<uint8> encodeRecord( const ValeProfile& profile );
        /** @brief 레코드 바이트를 읽습니다. 판이 다르거나 깨졌으면 false 입니다. */
        [[nodiscard]] static bool decodeRecord( const vector<uint8>& bytes, ValeProfile& outProfile );

        /** @brief 일이 결과를 두는 곳입니다(일과 저장소가 함께 든다 — 파일 머리말). */
        struct Completion
        {
            vector<uint8>      _bytes{};
            uint64             _accountID{ 0 };
            uint64             _version{ 0 }; ///< 읽은 판 · 쓴 판
            ServiceStoreResult _result{ ServiceStoreResult::Unavailable };
            uint8              _bWrite{ SW_FALSE };
        };
        struct Inbox
        {
            mutex              _mutex;
            vector<Completion> _listCompletion{}; ///< `_mutex` 아래 — 완료는 저장소를 거두는 스레드가, 꺼내기는 링크가 한다

            void push( Completion completion )
            {
                ScopedLock<mutex> lock{ _mutex };
                _listCompletion.push_back( std::move( completion ) );
            }
        };

    private:
        /** @brief 계정 하나의 기억입니다. */
        struct AccountState
        {
            vector<uint8>          _storedBytes{};                            ///< 저장소에 있다고 아는 바이트(읽었거나 쓴 것). 비면 레코드가 없다
            vector<uint8>          _writingBytes{};                           ///< 맡긴 쓰기의 바이트(`_bWriting`)
            vector<uint8>          _queuedBytes{};                            ///< 쓰는 중에 들어온 가장 새 바이트(`_bQueued`)
            uint64                 _version{ ServiceRecord::kAbsentVersion }; ///< 쓰기 조건으로 걸 판
            uint8                  _bKnown       : 1;                         ///< 판을 안다(읽었거나 쓴 뒤다)
            uint8                  _bWriting     : 1;                         ///< 맡긴 쓰기가 아직 거두어지지 않았다
            uint8                  _bQueued      : 1;                         ///< 쓰기가 끝나면 맡길 바이트가 있다
            uint8                  _bLoadWaiting : 1;                         ///< 쓰기가 끝나면 맡길 읽기가 있다
            uint8                  _bReleased    : 1;                         ///< 세션이 끝났다 — 쓰기가 끝나면 지운다
            [[maybe_unused]] uint8 _reserved     : 3;

            AccountState()
                : _bKnown{ SW_FALSE }
                , _bWriting{ SW_FALSE }
                , _bQueued{ SW_FALSE }
                , _bLoadWaiting{ SW_FALSE }
                , _bReleased{ SW_FALSE }
                , _reserved{ 0 }
            {
            }
        };

        /** @brief 지금 저장소입니다. 없으면 오류를 남기고 nullptr 입니다. */
        IServiceStore* findStore( const utf8* pPurpose, uint64 accountID ) const;
        void           submitRead( uint64 accountID );
        void           submitWrite( uint64 accountID, AccountState& state, vector<uint8> bytes );
        /** @brief 쓰기가 남지 않은 계정의 기다리던 읽기 · 지우기를 처리합니다. 계정을 지웠으면 true 입니다. */
        bool settleAccount( uint64 accountID, AccountState& state );

    private:
        ValeServiceStoreSource              _source;
        shared_ptr<Inbox>                   _pInbox;
        unordered_map<uint64, AccountState> _mapAccount;
        vector<Completion>                  _listCompletionScratch;
        uint64                              _savedCount;
        uint64                              _skippedSaveCount;
        int32                               _pendingJobCount;
    };
} // namespace sw
