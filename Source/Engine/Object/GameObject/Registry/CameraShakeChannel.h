/**
 * @file CameraShakeChannel.h
 * @brief 카메라 흔들림 요청 값과, 씬의 카메라 레지스트리가 소유하는 요청 채널입니다.
 * @details 요청을 내는 쪽(애니메이션 알림의 `CameraShake` 처리기)은 카메라를 모르고, 받는 쪽(게임 프레임워크의 카메라 매니저)은 알림을 모른다.
 *          둘은 씬(`GameObjectManager::getCameraRegistry`)의 채널 하나로 만난다 — 엔진 수명 정적 델리게이트를 두면 씬이 여럿일 때(에디터 미리보기 ·
 *          분할 월드) 다른 씬의 카메라까지 흔들리고, 등록 목록을 지키는 것이 없다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include <thread>

namespace sw
{
    /** @brief 알림이 낸 카메라 흔들림 요청입니다(카메라 쪽 충격 서술과 같은 필드이며 크기, 길이, 진동수, 거리 감쇠를 담습니다). */
    struct CameraShakeRequest
    {
        float3  _origin{};
        uint64  _sourceObjectID{ 0 };
        float32 _amplitude{ 0.1f };
        float32 _duration{ 0.3f };
        float32 _frequency{ 30.0f };
        float32 _falloffRadius{ 0.0f };
    };

    using CameraShakeRequestCallback = Delegate<void( const CameraShakeRequest& )>;
} // namespace sw

namespace sw
{
    /**
     * @class CameraShakeChannel
     * @brief 카메라 흔들림 요청을 듣는 쪽의 목록입니다. `bind` 가 돌려주는 구독이 등록을 소유합니다.
     * @details 스레드: 등록 · 해제 · 전달이 한 잠금을 쓰고, 전달은 잠금을 쥔 채 듣는 쪽을 부른다. 그래서 다른 스레드의 해제(구독 소멸)는 진행 중인
     *          전달이 끝날 때까지 기다린다 — 풀린 듣는 쪽이 불리는 일과, 워커의 전달과 게임 스레드의 등록이 목록을 함께 고치는 일이 형식으로 없다.
     *          잠금을 쥔 스레드가 다시 들어오면(듣는 쪽 안에서 등록 · 해제 · 전달) 잠금을 다시 잡지 않고, 해제는 바깥 전달이 끝난 뒤 지운다.
     *          듣는 쪽은 이 채널 밖의 잠금을 잡지 않는다(자기 값만 고친다).
     */
    class CameraShakeChannel final : public SubscriptionSource
    {
    public:
        CameraShakeChannel()           = default;
        ~CameraShakeChannel() override = default;

        CameraShakeChannel( const CameraShakeChannel& )            = delete;
        CameraShakeChannel& operator=( const CameraShakeChannel& ) = delete;

        /** @brief @p listener 를 등록하고 그 등록을 소유하는 구독을 반환합니다. 구독이 사라지면 빠집니다. 어느 스레드에서 불러도 됩니다. */
        [[nodiscard]] Subscription bind( const CameraShakeRequestCallback& listener )
        {
            uint64 id = 0;
            {
                const ChannelLock lock{ *this };
                id = ++_lastListenerID;
                _listListener.push_back( Listener{ id, listener } );
            }
            return issueSubscription( id );
        }

        /** @brief 등록된 모든 듣는 쪽에 @p request 를 전달합니다. 어느 스레드에서 불러도 됩니다. */
        void broadcast( const CameraShakeRequest& request )
        {
            const ChannelLock lock{ *this };
            ++_broadcastDepth;
            for ( size_t listenerIndex = 0; listenerIndex < _listListener.size(); ++listenerIndex )
            {
                // 듣는 쪽이 등록하면 벡터가 재할당되므로 원소 참조를 들고 부르지 않는다.
                const CameraShakeRequestCallback callee = _listListener[listenerIndex]._callback;
                if ( callee.isBound() )
                    callee( request );
            }
            --_broadcastDepth;
            if ( _broadcastDepth > 0 )
                return;
            // 전달 중에 풀린 자리(빈 콜백)를 지운다.
            size_t keptCount = 0;
            for ( size_t listenerIndex = 0; listenerIndex < _listListener.size(); ++listenerIndex )
            {
                if ( _listListener[listenerIndex]._callback.isBound() == false )
                    continue;
                if ( keptCount != listenerIndex )
                    _listListener[keptCount] = std::move( _listListener[listenerIndex] );
                ++keptCount;
            }
            _listListener.resize( keptCount );
        }

        /** @brief 등록된 듣는 쪽이 있는지 반환합니다. */
        bool isBound() const
        {
            const ChannelLock lock{ *this };
            return _listListener.empty() == false;
        }

    private:
        /** @brief 등록 하나입니다. */
        struct Listener
        {
            uint64                     _id{ 0 };
            CameraShakeRequestCallback _callback;
        };

        /** @brief 채널 잠금입니다. 이 스레드가 이미 쥐고 있으면(듣는 쪽 안의 재진입) 다시 잡지 않습니다. */
        class ChannelLock
        {
        public:
            explicit ChannelLock( const CameraShakeChannel& channel )
                : _channel{ channel }
                , _bOwned{ channel._lockOwnerThread.load() != std::this_thread::get_id() }
            {
                if ( _bOwned )
                {
                    _channel._mutex.lock();
                    _channel._lockOwnerThread.store( std::this_thread::get_id() );
                }
            }
            ~ChannelLock()
            {
                if ( _bOwned )
                {
                    _channel._lockOwnerThread.store( std::thread::id{} );
                    _channel._mutex.unlock();
                }
            }
            ChannelLock( const ChannelLock& )            = delete;
            ChannelLock& operator=( const ChannelLock& ) = delete;

        private:
            const CameraShakeChannel& _channel;
            const bool                _bOwned;
        };

        void releaseSubscription( uint64 id ) override
        {
            const ChannelLock lock{ *this };
            for ( size_t listenerIndex = 0; listenerIndex < _listListener.size(); ++listenerIndex )
            {
                if ( _listListener[listenerIndex]._id != id )
                    continue;
                if ( _broadcastDepth > 0 )
                    _listListener[listenerIndex]._callback = CameraShakeRequestCallback{};
                else
                    _listListener.erase( _listListener.begin() + static_cast<ptrdiff_t>( listenerIndex ) );
                return;
            }
        }

        mutable mutex                   _mutex;               ///< 목록과 전달을 지킨다
        mutable atomic<std::thread::id> _lockOwnerThread;     ///< 잠금을 쥔 스레드(재진입 판정). 없으면 빈 id
        vector<Listener>                _listListener;        ///< 등록 순서
        uint64                          _lastListenerID{ 0 }; ///< 마지막으로 내준 등록 번호
        uint32                          _broadcastDepth{ 0 }; ///< 전달 중첩 깊이(0 이 되면 해제된 자리를 지운다)
    };
} // namespace sw
