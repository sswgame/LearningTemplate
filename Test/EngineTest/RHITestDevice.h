/**
 * @file Test/EngineTest/RHITestDevice.h
 * @brief 실제 창 하나와 그 위의 RHI 디바이스 하나 — 스코프를 벗어나면 디바이스 → 창 순서로 내린다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestRunOptions.h"

#include <initializer_list>

namespace test
{
    class ScopedTrace;
} // namespace test

namespace test
{
    /**
     * @brief 네 백엔드 전부. 이 호스트에 없는 것(리눅스의 DX)은 `RHITestDevice` 가 창을 띄우기 전에 거르므로 목록을 플랫폼마다
     *        가를 필요가 없다.
     */
    inline constexpr sw::RHIBackend kArrAllRHIBackend[] = { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan,
                                                            sw::RHIBackend::OpenGL };

    /**
     * @brief 백엔드 스위치(`-dx11` · `-dx12` · `-vk` · `-gl`) 네 개 — 백엔드마다 케이스 하나로 가르는 `SW_TEST_CASE_PER_PARAMETER( …, test::listRHIBackendParameter, 비용 )` 의 목록이다.
     * @details 케이스 안의 `RHIBackendSweep` 은 그 케이스의 백엔드 하나로 범위를 좁힌다(`RHIBackendSweep` 머리말) — 몸통을 고치지 않고 `Suite.이름.<dx12|dx11|vk|gl>` 로 갈린다.
     */
    sw::vector<sw::string> listRHIBackendParameter();
    /** @brief @p backend 가 지금 케이스의 백엔드인가 — 백엔드마다 케이스로 가른 케이스면 그 백엔드 하나만, 그 밖의 케이스는 항상 true 다. 직접 백엔드 루프를 도는 가른 케이스가 쓴다. */
    bool matchesCaseBackend( sw::RHIBackend backend );

    /**
     * @brief 디바이스가 필요한 케이스의 창 + 디바이스 한 벌(`RHIDeviceTest` · `RenderPassGPUTest`).
     * @details 창 만들기 · 디바이스 만들기 · 표면 붙이기 · 초기화 · 실패 시 되감기를 한 곳에 모으고, 내리기는 소멸자가 한다.
     *          케이스가 끝의 내리기를 손으로 들면 그 사이의 `SW_ASSERT_*` 가 실패할 때 **내리기에 닿지 않는다** — 디바이스는
     *          `shutdown()` 없이 소멸자만 돌고 창은 `destroy()` 되지 않은 채 다음 케이스로 넘어간다.
     *
     *          스마트 포인터처럼 쓴다(`device->beginFrame( … )`, `renderer.initialize( device.get() )`). 디바이스가 서지
     *          않았으면 `isReady()` 가 false 이고 `get()` 은 널이다 — 그 백엔드가 이 호스트에 없거나 초기화에 실패한 것이다.
     */
    class RHITestDevice
    {
    public:
        /** @brief 아직 세우지 않은 빈 것. `initialize` 로 세운다. */
        RHITestDevice() = default;
        /** @brief 이 백엔드로 세운다. 실패하면 `isReady()` 가 false 다. */
        explicit RHITestDevice( sw::RHIBackend backend );
        /**
         * @brief 목록에서 **처음으로 서는** 백엔드로 세운다. 다 실패하면 `isReady()` 가 false 다.
         * @details 백엔드마다 가른 케이스(`SW_TEST_CASE_PER_PARAMETER( …, listRHIBackendParameter, … )`) 안에서는 목록이 그 케이스의 백엔드 하나로 좁아진다(목록에 없으면 서지 않는다).
         */
        explicit RHITestDevice( std::initializer_list<sw::RHIBackend> listBackend );
        ~RHITestDevice();

        RHITestDevice( const RHITestDevice& )            = delete;
        RHITestDevice& operator=( const RHITestDevice& ) = delete;
        RHITestDevice( RHITestDevice&& )                 = delete;
        RHITestDevice& operator=( RHITestDevice&& )      = delete;

        /** @brief 이 백엔드로 창과 디바이스를 세운다. 이미 서 있던 것은 먼저 내린다. 실패하면 아무것도 남기지 않는다. */
        bool initialize( sw::RHIBackend backend );
        /** @brief 디바이스와 창을 지금 내린다(소멸자도 부른다). 두 번 불러도 된다. */
        void shutdown();

        /** @brief 창은 두고 디바이스만 내린다 — 디바이스를 잃었을 때 무엇이 살아남는지 볼 때. 내리기 전에 GPU 를 기다린다. */
        void shutdownDevice();
        /** @brief 같은 창 위에 같은 백엔드로 디바이스를 새로 세운다 — 앱의 디바이스 교체 경로와 같은 순서다. */
        [[nodiscard]] bool recreateDevice();

        /** @brief 디바이스가 서 있는가. */
        bool isReady() const { return _device != nullptr; }
        /** @brief 디바이스(서지 않았으면 널). */
        sw::IRHIDevice* get() const { return _device.get(); }
        sw::IRHIDevice* operator->() const { return _device.get(); }
        sw::IRHIDevice& operator*() const { return *_device; }
        /** @brief 세운(또는 마지막으로 세우려 한) 백엔드. */
        sw::RHIBackend getBackend() const { return _backend; }

    private:
        sw::unique_ptr<sw::IWindow>    _window;
        sw::unique_ptr<sw::IRHIDevice> _device;
        sw::RHIBackend                 _backend{ sw::RHIBackend::DirectX11 };
    };
} // namespace test

namespace test
{
    /**
     * @brief 백엔드 목록을 차례로 세워 도는 범위 — `for ( test::RHITestDevice& device : sweep )` 의 몸통은 **서는 백엔드마다** 한 번 돈다.
     * @details 서지 않는 백엔드(이 빌드 · 호스트에 없거나 초기화 실패)는 건너뛴다. 다음 백엔드를 세우기 전에 앞 디바이스를 내리고, 다 돌면
     *          마지막 것도 내린다 — 몸통에 지역 디바이스를 두고 반복마다 소멸시키는 것과 같은 수명이다. 몸통의 `continue` 는 다음 백엔드로 간다.
     *
     *          하나도 서지 않았으면 `getReadyCount()` 가 0 이다. `SW_TEST_SKIP` 은 케이스 몸통에서만 return 할 수 있으므로 건너뛰기는
     *          케이스가 한다(`if ( sweep.getReadyCount() == 0 ) SW_TEST_SKIP( ... );`). 범위는 한 번만 돈다.
     *          몸통이 도는 동안 `backend=<이름>` 추적(`test::ScopedTrace`)이 걸려, 진 단언과 재현 요약이 어느 백엔드였는지 적는다.
     *
     *          **백엔드마다 케이스로 가른 케이스**(`SW_TEST_CASE_PER_PARAMETER( …, listRHIBackendParameter, … )`) 안에서는 어느 생성자로 만들었든 범위가 그 케이스의 백엔드 하나로 좁아진다
     *          (목록에 없는 백엔드면 비어 `getReadyCount()` 가 0 이다 — 케이스가 건너뛴다). 그 밖의 케이스는 목록 전부를 돈다. 백엔드끼리 결과를 견주는 케이스는 가르지 않는다.
     */
    class RHIBackendSweep
    {
    public:
        /** @brief 범위 for 가 쓰는 입력 반복자. 증가가 다음으로 서는 백엔드를 세운다. */
        class Iterator
        {
        public:
            explicit Iterator( RHIBackendSweep* pSweep )
                : _pSweep{ pSweep }
            {
            }

            RHITestDevice& operator*() const { return _pSweep->_device; }
            Iterator&      operator++()
            {
                _pSweep->advance();
                return *this;
            }
            /**
             * @brief 끝(널 스윕)과 비교할 때만 뜻이 있다 — 스윕의 디바이스가 서 있지 않으면 다 돈 것이다.
             * @note 범위 for 는 몸통 → 증가(다음 백엔드를 세움) → 비교 순서라, 몸통이 디바이스를 내려도 반복이 끝나지 않는다.
             */
            bool operator!=( const Iterator& other ) const { return isDone() != other.isDone(); }

        private:
            bool isDone() const { return _pSweep == nullptr || _pSweep->_device.isReady() == false; }

            RHIBackendSweep* _pSweep;
        };

        /** @brief 네 백엔드 전부(`kArrAllRHIBackend`)를 돈다. */
        RHIBackendSweep();
        /** @brief 이 순서로 돈다. */
        explicit RHIBackendSweep( std::initializer_list<sw::RHIBackend> listBackend );
        /** @brief 이 순서로 돈다(실행 중에 순서를 정한 목록). */
        explicit RHIBackendSweep( const sw::vector<sw::RHIBackend>& listBackend );

        ~RHIBackendSweep();
        RHIBackendSweep( const RHIBackendSweep& )            = delete;
        RHIBackendSweep& operator=( const RHIBackendSweep& ) = delete;
        RHIBackendSweep( RHIBackendSweep&& )                 = delete;
        RHIBackendSweep& operator=( RHIBackendSweep&& )      = delete;

        /** @brief 첫 번째로 서는 백엔드를 세운다. 한 번만 부른다(범위 for 가 부른다). */
        Iterator begin();
        Iterator end() { return Iterator{ nullptr }; }

        /** @brief 지금까지 선 백엔드 수 — 몸통이 돈 횟수다. */
        uint32 getReadyCount() const { return _readyCount; }

    private:
        /** @brief 앞 디바이스를 내리고 목록의 다음 백엔드 중 서는 것을 세운다. 더 없으면 디바이스가 빈 채로 끝난다. */
        void advance();

        RHITestDevice               _device;
        sw::unique_ptr<ScopedTrace> _pTrace; ///< 지금 백엔드의 추적 — 다음 백엔드를 세우기 전에 내린다
        sw::vector<sw::RHIBackend>  _listBackend;
        size_t                      _nextIndex;
        uint32                      _readyCount;
    };
} // namespace test
