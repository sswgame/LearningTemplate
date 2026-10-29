/**
 * @file GPUPassTimeline.h
 * @brief 패스별 GPU 타임스탬프를 읽어 프레임 프로파일러(`GPU.<패스>`)와 외부 프로파일러(Tracy)의 GPU 타임라인으로 내보냅니다.
 * @details 렌더 스레드가 프레임 끝에 한 번 부릅니다(패스 수만큼 도는 루프 하나). 타임스탬프를 **쓰는** 일은 패스를 기록하는 쪽
 *          (`FramePassExecutor::submitGraph` 와 `onGraphPassExecute`)이 하고, 쓸지 말지는 이 클래스의 `isRecording` 이 정합니다.
 *          프로파일러는 렌더러 초기화 때 주입받으므로 테스트가 자기 `FrameProfiler` 로 보고 결과를 확인할 수 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Diagnostics/RHITimestamp.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimelineExporter.h"

namespace sw
{
    struct RenderGraphPassDesc;

    class FrameProfiler;
    class IRHIDevice;

    /**
     * @class GPUPassTimeline
     * @brief 지난 프레임의 GPU 타임스탬프 한 벌과 그것을 보고할 프로파일러 슬롯입니다. 렌더 스레드만 씁니다.
     */
    class SW_API GPUPassTimeline
    {
    public:
        GPUPassTimeline();

        GPUPassTimeline( const GPUPassTimeline& )            = delete;
        GPUPassTimeline& operator=( const GPUPassTimeline& ) = delete;

        /**
         * @brief 보고할 프로파일러를 정합니다. nullptr 이면 타임스탬프를 쓰지도 읽지도 않습니다.
         * @details 슬롯 번호는 프로파일러마다 다르므로 다른 프로파일러로 바뀌면 등록해 둔 슬롯을 버리고 다시 등록합니다.
         */
        void bindProfiler( FrameProfiler* pProfiler );
        /** @brief 이번 프레임에 타임스탬프를 써야 하면 true 입니다. 프로파일러가 있고 켜져 있을 때만 씁니다(계측 스위치). */
        bool isRecording() const;

        /**
         * @brief 지난 프레임의 타임스탬프를 디바이스에서 읽어 프로파일러와 외부 GPU 타임라인으로 냅니다. 프레임 끝에 한 번 부릅니다.
         * @details **몇 프레임 늦은 값입니다.** 기다려서 최신 값을 받으면 측정하려던 파이프라인을 멈춰 세워 측정값이 거짓이 됩니다.
         *          타임스탬프 능력이 없는 디바이스(테스트 목)는 아무것도 하지 않습니다. Shipping 에서는 본문이 없습니다.
         */
        void readAndReport( IRHIDevice* pDevice, const vector<RenderGraphPassDesc>& listPass );
        /**
         * @brief 이미 읽은 타임스탬프 한 프레임을 프로파일러의 `GPU.<패스>`, `GPU.Compute`, `GPU.Frame` 구간으로 넣습니다. GPU 없이 부를 수 있습니다.
         * @details 음수 슬롯은 그 프레임에 기록되지 않았다는 표시입니다(패스를 건너뛰었거나 첫 사이클). 한쪽만 음수여도 구간이 성립하지 않습니다.
         *          `GPU.Frame` 은 프레임의 첫 명령부터 마지막 패스 끝까지이고, 패스 합과의 차이가 컴퓨트 프리패스, 업로드, 배리어입니다.
         */
        void reportFrame( const RHITimestampFrame& frame, const vector<RenderGraphPassDesc>& listPass );
        /** @brief 디바이스를 잊습니다. 다음 디바이스(백엔드 교체)는 새 GPU 컨텍스트를 엽니다. 같은 주소에 새 디바이스가 생겨도 옛 시계 기준을 쓰지 않습니다. */
        void forgetDevice();

    private:
        /**
         * @brief 방금 읽은 타임스탬프를 외부 프로파일러(Tracy)의 GPU 타임라인으로 내보냅니다. 출력이 꺼져 있으면 아무것도 하지 않습니다.
         * @details 디바이스가 바뀌면 GPU 컨텍스트를 새로 열고(그때 GPU 시계를 한 번 읽습니다), 읽기가 싼 시계(DX12, GL)는 몇 프레임마다 다시 맞춥니다.
         */
        void exportToExternalTimeline( IRHIDevice* pDevice );
        /**
         * @brief 패스 @p passIndex 의 `GPU.<패스>` 프로파일러 슬롯입니다. 그 슬롯의 패스 이름이 바뀌었을 때만 다시 등록합니다.
         * @details 파이프라인을 다시 읽어 패스 구성이 바뀌어도 이름 비교로 알아챈다(슬롯마다 문자열 비교 한 번).
         */
        uint32 getOrCreateScopeSlot( size_t passIndex, const string& passName );

    private:
        /** @brief 패스 하나의 GPU 스코프입니다. 그 슬롯을 만든 패스 이름, 프로파일러 슬롯, 외부 프로파일러 지점을 담습니다. */
        struct PassScope
        {
            string                 _passName;
            const ProfileZoneSite* _pZoneSite{ nullptr }; ///< Tracy GPU 구간 이름(패스 이름이며 프로세스 수명 동안 유지되는 사본)
            uint32                 _profilerSlot{ 0xFFFFFFFFu };
        };

    private:
        /** @brief 지난 프레임의 타임스탬프입니다. 단위는 마이크로초이며, 프레임 시작 기준 누적값에 기준점의 GPU 시계를 더한 값입니다. 엔진 테이블과 Tracy 가 같은 값을 씁니다. */
        RHITimestampFrame _frame;
        /**
         * @brief 패스 번호에서 GPU 스코프 슬롯을 찾는 목록입니다. 패스 이름이 그대로면 매 프레임 슬롯만 꺼냅니다.
         * @details `registerScope` 는 등록된 스코프 모두(약 80개)를 문자열 비교로 훑습니다. 그래서 렌더 스레드가 매 프레임 부르지 않고,
         *          슬롯의 패스 이름이 바뀔 때만 부릅니다. 프로파일러는 이름 **포인터**를 보관하므로 이름은 intern 아레나(프로세스가 끝날 때까지
         *          제자리에 있습니다)에 둡니다. 주의: 늘어나며 옮겨지는 저장소의 `string` 을 넘기면, 짧은 이름(`GPU.Shadow` 처럼 문자열 객체 안에
         *          들어가는 것)의 포인터가 옮겨진 뒤의 빈 공간을 가리킵니다.
         */
        vector<PassScope> _listPassScope;
        /// @brief 외부 프로파일러(Tracy)의 GPU 타임라인입니다. 엔진 테이블과 같은 타임스탬프를 쓰므로 쿼리는 한 벌만 있습니다.
        GPUTimelineExporter _exporter;
        /// @brief 패스 번호에서 Tracy 지점을 찾는 목록입니다. `_listPassScope` 에서 프레임마다 채우며, 할당을 반복하지 않도록 보관합니다.
        vector<const ProfileZoneSite*> _listPassSite;
        /// @brief 보고할 프로파일러입니다. 렌더러 초기화 때 주입받습니다.
        FrameProfiler* _pProfiler;
        /// @brief GPU 시계를 읽지 못한 디바이스입니다. 블로킹 읽기(DX11, Vulkan)를 프레임마다 다시 하지 않기 위해 기억합니다.
        const IRHIDevice* _pFailedDevice;
        /// @brief `GPU.Compute` 와 `GPU.Frame` 의 프로파일러 슬롯입니다. 처음 한 번만 등록하므로 프레임마다 선형 탐색을 하지 않습니다.
        uint32 _computeScopeSlot;
        uint32 _frameScopeSlot;
    };
} // namespace sw
