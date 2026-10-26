/**
 * @file SceneTransformStorage.cpp
 * @brief 트랜스폼 저장소 구현입니다. 칸 받기 · 놓기와 월드 합성 한 곳입니다.
 */
#include "pch.h"

#include "Engine/Object/Component/SceneTransformStorage.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/Memory.h"

#include <cstdlib>

namespace sw
{
    SW_LOG_CALLER( "SceneTransformStorage" );

    namespace
    {
        /** @brief 프로세스에 하나인 저장소입니다. 엔진 수명 범위가 닫히면(엔진 종료) 페이지와 함께 소멸합니다. */
        ScopeOwned<SceneTransformStorage> s_sceneTransformStorage;
    } // namespace

    SceneTransformStorage& SceneTransformStorage::get()
    {
        // 엔진 수명 범위가 닫힌 뒤의 접근이다 — 컴포넌트가 엔진보다 오래 살았다. 칸을 줄 저장소가 없으므로 모든 구성에서 멈춘다(`getRequired`).
        return s_sceneTransformStorage.getRequired();
    }

    SceneTransformStorage::SceneTransformStorage()
        : _arrPage{}
        , _listFreeSlot{}
        , _nextSlot{ 0 }
        , _liveSlotCount{ 0 }
        , _mutex{}
    {
    }

    SceneTransformStorage::~SceneTransformStorage()
    {
        // 엔진 종료(수명 범위 닫기) 때 칸이 남았으면 그 컴포넌트가 새었다. 페이지는 그래도 놓는다 — 샌 컴포넌트는 다시 불리지 않는다.
        if ( _liveSlotCount != 0 )
            SW_LOG_WARNING( "Scene transform storage still has %# live slots at shutdown - a scene component leaked", _liveSlotCount );
        for ( atomic<SceneTransformPage*>& page : _arrPage )
        {
            sw_delete( page.load( std::memory_order_relaxed ) );
            page.store( nullptr, std::memory_order_relaxed );
        }
    }

    uint32 SceneTransformStorage::allocateSlot( SceneComponent* pOwner, SceneTransformPage*& pOutPage )
    {
        uint32              slot  = kInvalidSlot;
        SceneTransformPage* pPage = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( tryPopFreeListIndex( _listFreeSlot, slot ) == false )
            {
                // 닿지 않는 한계다(`kMaxPageCount` 설명). 닿았다면 칸 없이 컴포넌트를 만들 방법이 없다 — 알리고 멈춘다.
                if ( _nextSlot >= kMaxPageCount * SceneTransformPage::kSlotCount )
                    SW_FATAL( "Scene transform storage is full (%# slots). Too many scene components are alive.", _nextSlot );
                slot = _nextSlot++;
            }

            // 페이지는 처음 닿을 때 만들어 release 로 발행한다. 칸 번호는 이 락을 나간 뒤에야 바깥에 알려지므로, 번호를 받은 쪽은 페이지를 본다.
            atomic<SceneTransformPage*>& pageEntry = _arrPage[slot >> SceneTransformPage::kSlotShift];
            pPage                                  = pageEntry.load( std::memory_order_relaxed );
            if ( pPage == nullptr )
            {
                pPage = make_unique<SceneTransformPage>().release(); // 락 없이 읽는 표에 올리는 자리에서만 소유를 놓는다
                pageEntry.store( pPage, std::memory_order_release );
            }
            ++_liveSlotCount;
        }

        // 칸은 이제 이 호출자 것이다. 채우는 데 락이 필요 없다.
        initializeSlot( *pPage, slot & SceneTransformPage::kSlotMask, pOwner );
        pOutPage = pPage;
        return slot;
    }

    void SceneTransformStorage::freeSlot( uint32 slot )
    {
        if ( slot == kInvalidSlot )
            return;
        SceneTransformPage* pPage = findPage( slot );
        if ( pPage == nullptr )
            return;

        // 놓은 칸이 누구의 것으로도 읽히지 않게 비운다. 틱 대기 표시도 내린다 — 파괴는 틱 밖이라 대기 목록에 남은 번호가 없다.
        const uint32 pageIndex               = slot & SceneTransformPage::kSlotMask;
        pPage->_arrOwner[pageIndex]          = nullptr;
        pPage->_arrPrimitiveIndex[pageIndex] = kNoPrimitive;
        pPage->_arrPendingMask[pageIndex]    = 0;
        pPage->_arrFlag[pageIndex]           = 0;

        std::scoped_lock<mutex> lock{ _mutex };
        _listFreeSlot.push_back( slot );
        --_liveSlotCount;
    }

    uint32 SceneTransformStorage::getLiveSlotCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _liveSlotCount;
    }

    void SceneTransformStorage::initializeSlot( SceneTransformPage& page, uint32 pageIndex, SceneComponent* pOwner )
    {
        page._arrWorldMatrix[pageIndex]      = float4x4::Identity;
        page._arrWorldPositionLwc[pageIndex] = double3{ 0.0, 0.0, 0.0 };
        page._arrLocalPosition[pageIndex]    = float3{ 0.0f, 0.0f, 0.0f };
        page._arrLocalRotation[pageIndex]    = float3{ 0.0f, 0.0f, 0.0f };
        page._arrLocalScale[pageIndex]       = float3{ 1.0f, 1.0f, 1.0f };
        page._arrRotationQuat[pageIndex]     = quaternion::Identity;
        page._arrRotationSource[pageIndex]   = float3{ 0.0f, 0.0f, 0.0f };
        page._arrPendingPosition[pageIndex]  = float3{ 0.0f, 0.0f, 0.0f };
        page._arrPendingRotation[pageIndex]  = float3{ 0.0f, 0.0f, 0.0f };
        page._arrPendingScale[pageIndex]     = float3{ 1.0f, 1.0f, 1.0f };
        page._arrOwner[pageIndex]            = pOwner;
        page._arrPrimitiveIndex[pageIndex]   = kNoPrimitive;
        page._arrPendingMask[pageIndex]      = 0;
        page._arrFlag[pageIndex]             = SceneTransformPage::kNotifyOwner;
        ++page._arrWorldVersion[pageIndex]; // 월드를 항등으로 썼다. 0 으로 되돌리지 않는다(`_arrWorldVersion` 설명)
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
