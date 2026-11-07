/**
 * @file PendingRequestTable.h
 * @brief 바깥에 맡기고 번호로 답을 받는 요청(접속 상태 찾기 · 외부 로그인 확인 · 연동 철회)을 완료 람다와 함께 들고, 열린 수를 세는 표입니다.
 * @details 서비스마다 `unordered_map< 요청 번호, 기다리는 값 >` 을 두고 결과 델리게이트에서 찾아 지우던 것(손으로 세는 대기)을 한 벌로 모은다 —
 *          `ServiceStoreTracker` 가 저장 왕복을 람다 둘로 맡기는 것과 같은 모양이다(UE `FOnlineAsyncTaskManager` 의 완료 델리게이트, C# `TaskCompletionSource` 표).
 *          - 완료 람다는 기다리는 값을 값으로 잡는다. `( const TResult&, PendingRequestEnd )` 면 취소(`cancelOwnedBy` · `cancelAll`)도 받고,
 *            `( const TResult& )` 면 답만 받는다(취소는 조용히 버린다).
 *          - 완료를 부르기 전에 줄을 지운다 — 완료가 새 요청을 맡기거나 열린 수를 물어도 맞다.
 *          - 주인 표(`ownerTag` — 보통 요청한 계정)로 그 주인의 요청만 취소한다(계정이 떠나면 기다리던 요청을 거둔다).
 *          - 서비스 스레드에서만 쓴다(잠금이 없다). 같은 열쇠가 열려 있으면 맡지 않고 false 다 — 열쇠는 바깥(제공자 · 접속 상태)이 내므로 부르는 쪽이 그 자리에서 끝낸다.
 * @code
 *     _verifications.track( key, accountID, [this, pending]( const PlatformLoginVerification& verification ) { finishVerification( pending, verification ); } );
 *     (void)_verifications.answer( key, verification );
 * @endcode
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include <type_traits>

namespace sw
{
    /** @brief 맡긴 요청 하나가 끝난 까닭입니다. */
    enum class PendingRequestEnd : uint8
    {
        Answered,  ///< 바깥이 답했다
        Cancelled, ///< 주인이 떠났거나 서비스가 내려가 답을 기다리지 않는다(결과 값은 기본값이다)
    };

    /**
     * @class PendingRequestTable
     * @brief 열쇠 @p TKey(요청 번호)마다 완료 람다 하나를 듭니다. @p THash 는 열쇠의 해시입니다.
     */
    template <typename TKey, typename TResult, typename THash = DefaultHash<TKey>>
    class PendingRequestTable
    {
    public:
        PendingRequestTable() = default;

        PendingRequestTable( const PendingRequestTable& )            = delete;
        PendingRequestTable& operator=( const PendingRequestTable& ) = delete;

        /**
         * @brief 요청 @p key 의 완료 @p complete 를 듭니다. @p ownerTag 는 `cancelOwnedBy` 가 고르는 주인입니다(없으면 0).
         * @return 같은 열쇠가 이미 열려 있으면 맡지 않고 false 입니다(완료는 불리지 않는다).
         */
        template <typename TComplete>
        bool track( const TKey& key, uint64 ownerTag, TComplete&& complete )
        {
            if ( _mapKeyToEntry.find( key ) != _mapKeyToEntry.end() )
                return false;
            _mapKeyToEntry.emplace( key, Entry{ make_unique<Completion<std::decay_t<TComplete>>>( std::forward<TComplete>( complete ) ), ownerTag } );
            return true;
        }

        /** @brief 요청 @p key 의 답 @p result 로 완료를 부릅니다. 모르는 열쇠(취소했거나 남의 답)면 false 입니다. */
        bool answer( const TKey& key, const TResult& result )
        {
            const auto entryIt = _mapKeyToEntry.find( key );
            if ( entryIt == _mapKeyToEntry.end() )
                return false;
            unique_ptr<ICompletion> pCompletion = std::move( entryIt->second._pCompletion );
            _mapKeyToEntry.erase( entryIt );
            pCompletion->invoke( result, PendingRequestEnd::Answered );
            return true;
        }

        /** @brief 주인 @p ownerTag 의 요청을 모두 취소로 끝냅니다. */
        void cancelOwnedBy( uint64 ownerTag )
        {
            vector<unique_ptr<ICompletion>> listCancelled;
            for ( auto entryIt = _mapKeyToEntry.begin(); entryIt != _mapKeyToEntry.end(); )
            {
                if ( entryIt->second._ownerTag != ownerTag )
                {
                    ++entryIt;
                    continue;
                }
                listCancelled.push_back( std::move( entryIt->second._pCompletion ) );
                entryIt = _mapKeyToEntry.erase( entryIt );
            }
            notifyCancelled( listCancelled );
        }

        /** @brief 열린 요청을 모두 취소로 끝냅니다. 서비스가 내려갈 때 부릅니다(완료가 잡은 객체가 아직 살아 있어야 한다). */
        void cancelAll()
        {
            vector<unique_ptr<ICompletion>> listCancelled;
            listCancelled.reserve( _mapKeyToEntry.size() );
            for ( auto& [key, entry] : _mapKeyToEntry )
            {
                (void)key;
                listCancelled.push_back( std::move( entry._pCompletion ) );
            }
            _mapKeyToEntry.clear();
            notifyCancelled( listCancelled );
        }

        /** @brief 답을 기다리는 요청 수입니다. */
        int32 getOpenCount() const { return static_cast<int32>( _mapKeyToEntry.size() ); }
        bool  isIdle() const { return _mapKeyToEntry.empty(); }

    private:
        /** @brief 완료 하나입니다(람다를 지운 형식). */
        class ICompletion
        {
        public:
            virtual ~ICompletion()                                              = default;
            virtual void invoke( const TResult& result, PendingRequestEnd end ) = 0;
        };

        template <typename TComplete>
        class Completion final : public ICompletion
        {
        public:
            explicit Completion( TComplete complete )
                : _complete{ std::move( complete ) }
            {
            }

            void invoke( const TResult& result, PendingRequestEnd end ) override
            {
                if constexpr ( std::is_invocable_v<TComplete&, const TResult&, PendingRequestEnd> )
                    _complete( result, end );
                else if ( end == PendingRequestEnd::Answered )
                    _complete( result );
            }

        private:
            TComplete _complete;
        };

        struct Entry
        {
            unique_ptr<ICompletion> _pCompletion;
            uint64                  _ownerTag{ 0 };
        };

        /** @brief 지운 줄들의 완료를 취소로 부릅니다(표를 다 고친 뒤 — 완료가 새 요청을 맡겨도 된다). */
        static void notifyCancelled( vector<unique_ptr<ICompletion>>& listCancelled )
        {
            const TResult none{};
            for ( unique_ptr<ICompletion>& pCompletion : listCancelled )
            {
                pCompletion->invoke( none, PendingRequestEnd::Cancelled );
            }
        }

    private:
        unordered_map<TKey, Entry, THash> _mapKeyToEntry;
    };
} // namespace sw
