/**
 * @file SceneTransformStorage.cpp
 * @brief 트랜스폼 저장소 구현입니다. 칸 받기 · 놓기와 월드 합성 한 곳입니다.
 */
#include "pch.h"

#include "Engine/Object/Component/SceneTransformStorage.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    SceneTransformStorage::SceneTransformStorage()
        : _store{}
    {
    }

    SceneTransformStorage::~SceneTransformStorage()
    {
        // 서비스 범위가 내려갈 때 칸이 남았으면 그 컴포넌트가 새었다. 페이지는 그래도 놓는다(`ComponentDataStore` 소멸) — 샌 컴포넌트는 다시 불리지 않는다.
        const uint32 liveSlotCount = _store.getLiveSlotCount();
        if ( liveSlotCount != 0 )
            SW_LOG_WARNING_ID( "SceneTransformStorage.SceneTransformStorageStill", "Scene transform storage still has %# live slots at shutdown - a scene component leaked", liveSlotCount );
    }

    uint32 SceneTransformStorage::allocateSlot( SceneComponent* pOwner, SceneTransformPage*& pOutPage )
    {
        return _store.allocateSlot( pOwner, pOutPage );
    }

    void SceneTransformStorage::freeSlot( uint32 slot )
    {
        _store.freeSlot( slot );
    }

    void SceneTransformPage::releaseSlot( uint32 pageIndex )
    {
        _arrOwner[pageIndex]          = nullptr;
        _arrPrimitiveIndex[pageIndex] = kNoPrimitive;
        _arrPendingMask[pageIndex]    = 0;
        _arrFlag[pageIndex]           = 0;
    }

    void SceneTransformPage::initializeSlot( uint32 pageIndex, SceneComponent* pOwner )
    {
        _arrWorldMatrix[pageIndex]      = float4x4::Identity;
        _arrWorldPositionLwc[pageIndex] = double3{ 0.0, 0.0, 0.0 };
        _arrLocalPosition[pageIndex]    = float3{ 0.0f, 0.0f, 0.0f };
        _arrLocalRotation[pageIndex]    = float3{ 0.0f, 0.0f, 0.0f };
        _arrLocalScale[pageIndex]       = float3{ 1.0f, 1.0f, 1.0f };
        _arrRotationQuat[pageIndex]     = quaternion::Identity;
        _arrRotationSource[pageIndex]   = float3{ 0.0f, 0.0f, 0.0f };
        _arrPendingPosition[pageIndex]  = float3{ 0.0f, 0.0f, 0.0f };
        _arrPendingRotation[pageIndex]  = float3{ 0.0f, 0.0f, 0.0f };
        _arrPendingScale[pageIndex]     = float3{ 1.0f, 1.0f, 1.0f };
        _arrOwner[pageIndex]            = pOwner;
        _arrPrimitiveIndex[pageIndex]   = kNoPrimitive;
        _arrPendingMask[pageIndex]      = 0;
        _arrFlag[pageIndex]             = kNotifyOwner;
        ++_arrWorldVersion[pageIndex]; // 월드를 항등으로 썼다. 0 으로 되돌리지 않는다(`_arrWorldVersion` 설명)
    }

    void SceneTransformStorage::composeWorld( SceneTransformPage& page, uint32 pageIndex, const float4x4* pParentWorld, const double3* pParentLwc )
    {
        // 값으로 한 번만 읽는다. 참조로 들면 아래의 "캐시와 같은가" 비교와 쿼터니언 만들기 · 캐시 적기가 칸을 따로 읽어, 그 사이에 칸이 바뀌면
        // 캐시(`source`)는 새 값인데 쿼터니언은 옛 값으로 남고 다음 합성이 그것을 다시 쓴다.
        const float3 position = page._arrLocalPosition[pageIndex];
        const float3 rotation = page._arrLocalRotation[pageIndex];
        const float3 scale    = page._arrLocalScale[pageIndex];

        // DirectX 행-벡터 규격: Scale * Rotation * Translation. 행렬 셋을 곱하지 않고 결과를 바로 적는다(`float4x4::makeTrs` 주석).
        // 회전이 없으면 회전 행렬은 단위 행렬이라 대각선에 스케일만 놓는다(격자 배치 · 파티클 · 떠다니는 소품이 흔하다).
        float4x4   localTrs;
        const bool bNoRotation = rotation._x == 0.0f && rotation._y == 0.0f && rotation._z == 0.0f;
        if ( bNoRotation )
        {
            localTrs = float4x4{ scale._x, 0.0f, 0.0f, 0.0f, 0.0f, scale._y, 0.0f, 0.0f, 0.0f, 0.0f, scale._z, 0.0f, position._x, position._y, position._z, 1.0f };
        }
        else
        {
            // 회전 변환 캐시(언리얼 `FRotationConversionCache` 의 자리). 오일러 → 쿼터니언은 삼각 함수 여섯 번이다. 회전은 그대로이고
            // 위치 · 스케일만 바뀌거나 부모만 움직인 합성이 대부분이라, 쿼터니언을 만든 오일러를 들고 있다가 같으면 건너뛴다.
            float3&     source           = page._arrRotationSource[pageIndex];
            quaternion& quat             = page._arrRotationQuat[pageIndex];
            const bool  bRotationChanged = source._x != rotation._x || source._y != rotation._y || source._z != rotation._z;
            if ( bRotationChanged )
            {
                quat   = quaternion::makeFromYawPitchRoll( rotation._y, rotation._x, rotation._z );
                source = rotation;
            }
            localTrs = float4x4::makeTrs( position, quat, scale );
        }

        float4x4& world = page._arrWorldMatrix[pageIndex];
        double3&  lwc   = page._arrWorldPositionLwc[pageIndex];
        if ( pParentWorld != nullptr && pParentLwc != nullptr )
        {
            world               = localTrs * ( *pParentWorld );
            const float3 offset = float3::transformVector( position, *pParentWorld );
            lwc                 = *pParentLwc + double3( static_cast<float64>( offset._x ), static_cast<float64>( offset._y ), static_cast<float64>( offset._z ) );
        }
        else
        {
            world = localTrs;
            lwc   = double3( static_cast<float64>( position._x ), static_cast<float64>( position._y ), static_cast<float64>( position._z ) );
        }
        // 월드를 쓰는 곳이 여기 하나라(세터 · 틱 대기 적용 · 쓰기 큐 배치 · 플러시 모두 이리 온다) 병렬 틱 경로도 번호를 빠뜨리지 않는다.
        ++page._arrWorldVersion[pageIndex];
    }
} // namespace sw

namespace sw
{
    // 트랜스폼 칸 저장소 — 서비스 범위라 엔진 범위의 어느 컴포넌트보다 나중에 내려간다(범위 안의 순서는 서브시스템이 선언한 의존이 정한다).
    SW_ENGINE_SERVICE_SUBSYSTEM( SceneTransformStorage, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( SceneTransformStorage ) );
} // namespace sw
