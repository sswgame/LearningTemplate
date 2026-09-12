/**
 * @file GpuUploadQueue.h
 * @brief GPU 리소스 **생성**을 렌더 스레드 밖으로 옮기는 큐.
 *
 * @details 렌더 스레드는 그리기만 해야 한다 — 생성은 그리기 전에 이미 끝나 있어야 한다. 예전에는 메시가
 *          `GpuScene::upload`(RT) 안에서 처음 그려질 때 만들어졌고, 그래서 새 메시가 등장한 프레임은 RT 가
 *          정점 버퍼 생성 비용을 통째로 뒤집어썼다(백엔드 교체처럼 전부 다시 올려야 할 때는 더 크다).
 *
 *          여기서는 게임 스레드가 "이번 프레임에 그릴 것" 을 알고 있으므로, 스냅샷을 내보내기 전에 아직
 *          올라가지 않은 것을 워커로 넘겨 **병렬로** 만든다. 만들기가 끝난 뒤에 스냅샷이 나가므로 팝인이
 *          없고 프레임 그림도 그대로다(스크린샷 비교가 계속 유효하다).
 *
 *          **RT 경로는 그대로 남는다.** `Mesh::initRhi` 는 이미 올라가 있으면 핸들만 돌려주는 멱등 함수라,
 *          큐가 먼저 만들어 두면 RT 의 그 호출은 값을 읽는 일이 된다. 큐가 꺼진 백엔드(OpenGL)나 큐가
 *          미처 다루지 못한 메시는 RT 가 예전처럼 그 자리에서 만든다 — 이 큐는 **앞당기는 장치**이지
 *          유일한 통로가 아니다. 그래서 이것 때문에 화면이 비는 경우는 없다.
 *
 * @note **왜 렌더 스레드와 워커가 같은 메시를 동시에 만들 수 없는가.** 게임 스레드는 스냅샷을 내보내기 전에
 *       flush 하므로, 어떤 메시든 그것이 든 패킷이 렌더 스레드에 닿기 전에 이미 상주한다. 렌더 스레드가 직전
 *       패킷을 그리며 같은 메시에 `upload()` 를 불러도 그때는 값을 읽을 뿐이다. 이 불변식이 깨지는 길은 하나뿐이다 —
 *       **flush 를 기다리지 않고** 스냅샷을 내보내는 것. 그래서 flush 는 동기다.
 *
 * @note 스레드 안전성은 백엔드가 말한다(`RHICapabilities::_bThreadSafeResourceCreation`). DX12 · DX11 ·
 *       Vulkan 은 버퍼 생성이 디바이스 레벨이고 핸들 테이블도 잠겨 있어 워커에서 안전하다. OpenGL 은
 *       `glGenBuffers` 가 **현재 컨텍스트**를 필요로 해서 안 된다 — 그 백엔드에서는 큐가 인라인으로
 *       동작해 예전과 같은 길이 된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IRHIDevice;
    class Mesh;
    class TaskManager;

    /**
     * @class GpuUploadQueue
     * @brief 올라가지 않은 GPU 리소스를 모아 워커에서 만들어 둡니다.
     * @note 게임 스레드에서만 요청·flush 한다. 큐 자체는 잠그지 않는다 — 잠금이 필요해지는 순간
     *       "누가 이 큐를 쓰는가" 가 흐려진 것이므로, 그때 소유를 다시 볼 것.
     */
    class SW_API GpuUploadQueue
    {
    public:
        GpuUploadQueue()  = default;
        ~GpuUploadQueue() = default;

        GpuUploadQueue( const GpuUploadQueue& )            = delete;
        GpuUploadQueue& operator=( const GpuUploadQueue& ) = delete;

        /**
         * @brief 디바이스와 태스크 매니저를 겁니다. 백엔드 교체 뒤에도 다시 부릅니다.
         * @details 디바이스가 바뀌면 쌓여 있던 요청은 옛 디바이스의 것이므로 버린다 — 새 디바이스에 필요한
         *          메시는 다음 프레임의 GT 가 다시 요청한다(상주 판단이 세대 기반이라 자동으로 걸린다).
         */
        void bindDevice( IRHIDevice* pDevice, TaskManager* pTaskManager );

        /** @brief 메시를 업로드 대상으로 올립니다. 이미 올라가 있으면 아무 일도 하지 않습니다. */
        void requestMesh( const shared_ptr<Mesh>& mesh );

        /**
         * @brief 쌓인 요청을 실제로 만듭니다. 끝날 때까지 기다립니다.
         * @details 워커가 병렬로 만들고 게임 스레드가 기다린다 — RT 가 직렬로 만들던 것을 GT 가 병렬로
         *          기다리는 것으로 바꾼 것이다. 기다리는 이유는 프레임 그림을 바꾸지 않기 위해서다.
         * @return 이번에 실제로 만든 개수.
         */
        uint32 flush();

        /** @brief 아직 만들지 않은 요청 수입니다. */
        uint32 getPendingCount() const { return static_cast<uint32>( _listPendingMesh.size() ); }

        /** @brief 워커로 병렬 생성이 가능한 백엔드면 true (OpenGL 은 false). */
        bool isParallel() const { return _bParallel == SW_TRUE; }

    private:
        /** @brief 워커가 쓰는 디바이스. 소유하지 않습니다. */
        IRHIDevice* _pDevice{ nullptr };
        /** @brief 병렬 생성을 돌릴 태스크 매니저. 소유하지 않습니다. */
        TaskManager* _pTaskManager{ nullptr };
        /**
         * @brief 이번 프레임에 만들 메시. **소유를 함께 든다** — 워커가 만드는 동안 게임 스레드가 놓아도
         *        메시가 사라지면 안 되기 때문이다(Graphics README 의 소유 규칙 1번과 같은 이유다).
         */
        vector<shared_ptr<Mesh>> _listPendingMesh;
        /** @brief 이 백엔드가 워커 생성을 허용하면 1. */
        uint8 _bParallel{ SW_FALSE };
    };
} // namespace sw
