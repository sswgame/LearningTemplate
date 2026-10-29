/**
 * @file MaterialCacheImpl.h
 * @brief 머티리얼 캐시의 내부 상태(`MaterialCache::Impl`)입니다. 캐시 본문과 핫 리로드(`MaterialCacheReload.cpp`)만 include 한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Material/MaterialCache.h"

#include <thread>

namespace sw
{
    class Material;

    struct MaterialCache::Impl
    {
        struct Entry
        {
            shared_ptr<Material> _material; ///< shared 인 이유는 Material.h 머리 주석에 있습니다(렌더 패킷이 소유를 빌립니다)
            string               _path;
            uint32               _refCount{ 0 };
        };

        unordered_map<string, Entry> _mapEntry;
        vector<string>               _listPendingKey; ///< `requestInitialize` 가 등록한, GPU 자원을 초기화할 경로(`_mutex` 아래)
        vector<string>               _listPSOWarmKey; ///< 고정(`pinAsset`)이 넣은, PSO 를 미리 만들 경로(`_mutex` 아래)
        atomic<uint32>               _pendingCount;   ///< `_listPendingKey` 의 크기. 0 이면 `initializePending` 이 잠그지 않는다(매 프레임 불린다)
        atomic<uint32>               _psoWarmCount;   ///< `_listPSOWarmKey` 의 크기. 0 이면 `takePSOWarmMaterials` 가 잠그지 않는다(매 프레임 불린다)
        shared_mutex                 _mutex;
        /// @brief 디바이스로 머티리얼을 초기화하는 구간(`DeviceInitScope`)을 지금 쥔 스레드입니다. 비어 있으면 아무도 안 쥐었습니다.
        atomic<std::thread::id> _initOwner;
        uint32                  _initDepth; ///< 쥔 스레드의 중첩 깊이입니다. 쥔 스레드만 읽고 씁니다

        Impl()
            : _mapEntry{}
            , _listPendingKey{}
            , _listPSOWarmKey{}
            , _pendingCount{ 0 }
            , _psoWarmCount{ 0 }
            , _mutex{}
            , _initOwner{ std::thread::id{} }
            , _initDepth{ 0 }
        {
        }

        /**
         * @brief 디바이스로 머티리얼을 초기화하는 구간을 한 스레드가 쥐게 합니다. 다른 스레드가 쥐고 있으면 오류를 남기고 멈춥니다(모든 구성).
         * @details 초기화는 `_mutex` 밖에서 한다(GL 은 렌더 스레드의 프레임 끝을 기다리는데 렌더 스레드가 `_mutex` 를 잡는다). 그래서 같은 머티리얼을 두 스레드가
         *          동시에 초기화하는 것을 막던 것이 이 구간뿐이다. 호출처는 모두 게임 스레드(`Scene::initialize` · `initializePending` · 에디터 `reload`)이고
         *          비용은 원자 비교 한 번이라 Release 에서도 확인한다. 같은 스레드의 중첩은 허용한다.
         */
        struct DeviceInitScope
        {
            Impl& _impl;
            bool  _bOwns{ false };

            /// @brief 쥘 수 없으면 기다리지도 멈추지도 않고 `owns()` 가 false 가 되는 생성입니다(렌더 스레드의 `initializePending`).
            struct TryTag
            {
            };

            DeviceInitScope( Impl& impl, TryTag )
                : _impl{ impl }
            {
                const std::thread::id self = std::this_thread::get_id();
                std::thread::id       expected{};
                if ( _impl._initOwner.compare_exchange_strong( expected, self ) )
                {
                    _impl._initDepth = 1;
                    _bOwns           = true;
                }
                else if ( expected == self )
                {
                    ++_impl._initDepth;
                    _bOwns = true;
                }
            }

            bool owns() const { return _bOwns; }

            explicit DeviceInitScope( Impl& impl )
                : _impl{ impl }
                , _bOwns{ true }
            {
                const std::thread::id self = std::this_thread::get_id();
                std::thread::id       expected{};
                if ( _impl._initOwner.compare_exchange_strong( expected, self ) )
                {
                    _impl._initDepth = 1;
                    return;
                }
                if ( expected == self )
                {
                    ++_impl._initDepth;
                    return;
                }
                SW_FATAL( "Materials are initialized on two threads at once - initialize them on the game thread (MaterialCache::acquire with a device, reload, initializePending)" );
            }

            ~DeviceInitScope()
            {
                if ( _bOwns == false )
                    return;
                if ( --_impl._initDepth == 0 )
                    _impl._initOwner.store( std::thread::id{} );
            }

            DeviceInitScope( const DeviceInitScope& )            = delete;
            DeviceInitScope& operator=( const DeviceInitScope& ) = delete;
        };
    };
} // namespace sw
