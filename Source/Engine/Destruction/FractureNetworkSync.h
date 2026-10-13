/**
 * @file FractureNetworkSync.h
 * @brief 파괴 컴포넌트가 네트워크와 주고받는 것을 맡습니다. 받는 쪽의 최근 상태 해시, `driveGroup` 이 쌓은 자세 목표, 스냅샷 바이트 형식입니다.
 * @details 그룹 바디와 렌더링을 고치는 일은 `FractureComponentBase` 가 한다. 이 클래스는 물리와 렌더링을 모르므로 엔진 없이 테스트할 수 있다.
 *          자세 목표는 어느 스레드에서든 쌓고, 게임 스레드의 물리 프레임이 한 번에 꺼내 간다(잠금 아래).
 *          구조 상태(`DestructionState`)와 사건 기록은 건드리지 않는다. 결정성은 사건 순서가 정하고, 이 클래스는 그 순서를 바꾸지 않는다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Network/Replication/TickRingBuffer.h"

namespace sw
{
    /** @brief 떨어진 그룹 하나의 현재 자세입니다. 네트워크 복제가 읽습니다. */
    struct FractureGroupPose
    {
        float3     _position{}; ///< 그룹 자세의 위치다(오브젝트 원점이 있는 월드 위치).
        float3     _center{};   ///< 월드 질량 중심이다. 보간과 오차는 이 값으로 본다. 원점은 덩어리에서 멀 수 있어서 회전하면 크게 움직인다.
        quaternion _rotation{};
        uint32     _groupID{ 0 };
        float32    _volume{ 0.0f };       ///< 배율을 곱한 부피다. 덩어리(`isChunkVolume`)면 서버가 자세를 보낸다.
        uint8      _bResting{ SW_FALSE }; ///< 잠들었거나 바디를 제거했다(멈췄다).
        uint8      _bGone{ SW_FALSE };    ///< 사라졌다
        uint8      _bDriven{ SW_FALSE };  ///< 네트워크가 자세를 정한다(클라이언트의 덩어리, 키네마틱).
        uint8      _bHasBody{ SW_FALSE }; ///< 바디가 있다(충돌한다).
        uint8      _layer{ 0 };           ///< 바디의 물리 레이어다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class FractureNetworkSync
     * @brief `FractureComponentBase` 의 값 멤버입니다. 컴포넌트가 직접(비가상으로) 부릅니다.
     */
    class SW_API FractureNetworkSync
    {
    public:
        static constexpr uint32 kRecentHashCount = 64; ///< 받는 쪽이 사건마다 기록해 두는 최근 상태 해시 수다.
        static constexpr uint32 kMagic           = FourCcUtil::make( "SWFS" );

        /** @brief 스냅샷 바이트를 읽은 결과입니다. 앞의 것부터 확인하며, 하나라도 실패면 상태를 바꾸지 않습니다. */
        enum class SnapshotReadResult : uint8
        {
            Success = 0,
            WrongMagic,     ///< 매직이 다르다.
            StateTruncated, ///< 구조 상태 바이트가 없거나 잘렸다.
            PoseTruncated,  ///< 그룹 자세가 잘렸다.
        };

        FractureNetworkSync();

        /** @brief 사건 수 @p eventCount 에 이르렀을 때의 상태 해시를 기록합니다. 처음 기록할 때 `kRecentHashCount` 슬롯을 잡습니다. */
        void recordStateHash( uint32 eventCount, uint64 hash );
        /** @brief 기록한 최근 해시 중 사건 수 @p eventCount 의 것을 찾습니다. 없으면 false 입니다. */
        [[nodiscard]] bool findStateHash( uint32 eventCount, uint64& outHash ) const;

        /** @brief 그룹 하나의 자세 목표를 쌓습니다. 같은 그룹의 목표가 이미 있으면 새 것으로 바꿉니다. 어느 스레드에서든 부를 수 있습니다. */
        void queueDriveTarget( const FractureGroupPose& target );
        /** @brief 쌓인 자세 목표를 모두 꺼냅니다. 쌓인 순서 그대로입니다. */
        void takeDriveTargets( vector<FractureGroupPose>& outListTarget );

        /** @brief 구조 상태 바이트(`DestructionState::writeSnapshot`)와 떨어진 그룹 자세를 스냅샷 바이트로 씁니다. 사라진 그룹은 번호와 사라짐 비트만 씁니다. */
        static void writeSnapshot( const vector<uint8>& stateBytes, const vector<FractureGroupPose>& listPose, vector<uint8>& outBytes );
        /** @brief `writeSnapshot` 이 쓴 바이트를 읽습니다. 구조 상태 바이트는 해석하지 않고 그대로 돌려줍니다(그래프에 맞는지는 부르는 쪽이 봅니다). */
        [[nodiscard]] static SnapshotReadResult readSnapshot( const vector<uint8>& bytes, vector<uint8>& outStateBytes, vector<FractureGroupPose>& outListPose );

    private:
        mutex                     _driveMutex;
        vector<FractureGroupPose> _listPendingDrive; ///< `queueDriveTarget` 이 쌓은 목표다(`_driveMutex` 아래).
        TickRingBuffer<uint64>    _listRecentHash;   ///< 사건 수로 찾는 최근 상태 해시다. 받는 쪽만 쓰므로 처음 기록할 때 슬롯을 잡는다.
    };
} // namespace sw
