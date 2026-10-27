/**
 * @file SceneTransformStorage.h
 * @brief 씬 컴포넌트 트랜스폼 값(로컬 TRS, 월드 행렬, LWC)의 전역 저장소입니다. 값마다 연속 배열이고, 컴포넌트는 슬롯 번호와 페이지만 가집니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class SceneComponent;
    template <class T>
    class ScopeOwned;

    /**
     * @struct SceneTransformPage
     * @brief 트랜스폼 슬롯 256 개를 담은 페이지입니다. 값마다 배열 하나라, 같은 값을 연달아 도는 패스(틱 뒤 적용, 플러시, 렌더 수집)가 라인 단위로 읽습니다.
     * @details 슬롯 하나의 값은 모든 배열의 같은 위치(`슬롯 번호 & kSlotMask`)에 있다. 페이지는 한 번 만들면 옮기지도 해제하지도 않으므로(저장소가
     *          엔진 수명 범위와 함께 소멸할 때까지), 컴포넌트는 자기 페이지 포인터를 가지고 테이블을 거치지 않고 찾는다.
     *
     *          값을 `SceneComponent` 안에 두지 않는 이유는 틱 뒤 적용과 렌더 수집이 8000 개를 돌 때 오브젝트마다 캐시 라인 서너 개를 건너다니고,
     *          행렬 하나를 읽으려고 컴포넌트 전체를 끌어오게 되기 때문이다.
     */
    struct alignas( 64 ) SceneTransformPage
    {
        static constexpr uint32 kSlotShift = 8;
        static constexpr uint32 kSlotCount = 1u << kSlotShift;
        static constexpr uint32 kSlotMask  = kSlotCount - 1u;

        /** @brief `_arrFlag` 비트입니다. 게임 스레드가 구조를 바꿀 때(붙이기, 떼기, 등록)만 씁니다. */
        enum SlotFlag : uint8
        {
            kHasParent   = 1u << 0, ///< 부모에 붙어 있다
            kHasChildren = 1u << 1, ///< 자식이 하나 이상 있다
            kNotifyOwner = 1u << 2, ///< 월드가 바뀌면 소유 컴포넌트의 `onWorldTransformUpdated` 를 부른다
        };
        /**
         * @brief 로컬 값 하나를 가리키는 비트입니다. 세터, 틱 대기 표시(`_arrPendingMask`), 쓰기 건(`SceneTransformWrite`)이 같은 비트를 씁니다.
         * @details 세 값을 다루는 곳(세터 세 개, 틱 대기, 쓰기 큐 합치기, 배치 적용, 대기 적용)은 세 분기로 분기하지 않고 이 비트로 고른다.
         */
        enum LocalValueBit : uint8
        {
            kLocalPosition = 1u << 0,
            kLocalRotation = 1u << 1,
            kLocalScale    = 1u << 2,
        };

        /** @brief 비트 하나가 가리키는 로컬 값의 위치입니다. */
        float3& getLocalValueRef( uint32 pageIndex, uint8 bit )
        {
            return ( bit == kLocalPosition ) ? _arrLocalPosition[pageIndex] : ( bit == kLocalRotation ) ? _arrLocalRotation[pageIndex]
                                                                                                        : _arrLocalScale[pageIndex];
        }
        /** @brief 비트 하나가 가리키는 틱 대기 값의 위치입니다. */
        float3& getPendingValueRef( uint32 pageIndex, uint8 bit )
        {
            return ( bit == kLocalPosition ) ? _arrPendingPosition[pageIndex] : ( bit == kLocalRotation ) ? _arrPendingRotation[pageIndex]
                                                                                                          : _arrPendingScale[pageIndex];
        }
        /**
         * @brief 로컬 값 하나를 씁니다. 현재 값과 거의 같으면 쓰지 않고 false 를 반환합니다.
         * @details **제곱 거리에는 제곱한 허용치를 쓴다.** `kEpsilon` 을 그대로 대면 실제 거리 1e-3 까지가 "안 움직였다"가 되는데, 비교
         *          기준이 매번 **현재 값**이라 그 아래 움직임은 쌓이지도 않는다. 한 프레임에 1e-3 보다 조금씩 가는 물체는 영원히 제자리에
         *          있게 된다. 세터, 틱 뒤 적용, 배치 쓰기가 모두 이 규칙 하나를 쓴다.
         */
        [[nodiscard]] bool writeLocalValue( uint32 pageIndex, uint8 bit, const float3& value )
        {
            float3& current = getLocalValueRef( pageIndex, bit );
            if ( float3::getDistanceSquared( current, value ) <= MathUtil::kEpsilonSquared )
                return false;
            current = value;
            return true;
        }

        float4x4        _arrWorldMatrix[kSlotCount];      ///< 계층을 합성한 월드 행렬
        double3         _arrWorldPositionLwc[kSlotCount]; ///< float64 로 누적한 월드 위치(LWC)
        float3          _arrLocalPosition[kSlotCount];    ///< 로컬 위치(PROPERTY `_localPosition`)
        float3          _arrLocalRotation[kSlotCount];    ///< 로컬 오일러 회전(PROPERTY `_localRotation`)
        float3          _arrLocalScale[kSlotCount];       ///< 로컬 스케일(PROPERTY `_localScale`)
        quaternion      _arrRotationQuat[kSlotCount];     ///< `_arrRotationSource` 로 만든 쿼터니언입니다(회전 변환 캐시)
        float3          _arrRotationSource[kSlotCount];   ///< `_arrRotationQuat` 을 만든 오일러 회전입니다
        float3          _arrPendingPosition[kSlotCount];  ///< 틱 중에 쓴 로컬 위치(적용 전)
        float3          _arrPendingRotation[kSlotCount];  ///< 틱 중에 쓴 로컬 회전(적용 전)
        float3          _arrPendingScale[kSlotCount];     ///< 틱 중에 쓴 로컬 스케일(적용 전)
        SceneComponent* _arrOwner[kSlotCount];            ///< 슬롯을 쓰는 컴포넌트다. 빈 슬롯이면 nullptr 이다
        uint32          _arrPrimitiveIndex[kSlotCount];   ///< 렌더 프리미티브 번호(`PrimitiveRegistry`). 없으면 `SceneTransformStorage::kNoPrimitive`
        uint8           _arrPendingMask[kSlotCount];      ///< 적용을 기다리는 틱 대기 값(`LocalValueBit` 조합). 슬롯의 주인 오브젝트를 틱하는 스레드만 쓴다
        uint8           _arrFlag[kSlotCount];             ///< `SlotFlag` 조합
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneTransformStorage
     * @brief 모든 씬 컴포넌트의 트랜스폼 값을 담는 전역 저장소입니다(유니티의 `TransformHierarchy` 에 해당).
     * @details **컴포넌트는 만들어질 때 슬롯을 받고 없어질 때 반환한다.** 씬에 붙지 않은 컴포넌트(스택에 만든 것, 기본값 비교용)도 슬롯이
     *          있으므로, 등록 여부에 따라 값의 위치가 바뀌는 일이 없다. 리플렉션은 `SceneComponent` 의 값 접근자(PROPERTY 를 붙인 참조
     *          반환 메서드)로 슬롯을 찾으므로 직렬화 키(`_localPosition` …)와 씬 파일은 그대로다.
     *
     *          씬(매니저)마다가 아니라 하나만 두는 이유: 슬롯 번호가 컴포넌트의 수명 동안 바뀌지 않아야 렌더 레지스트리와 틱 대기 목록이 번호만 가지고
     *          있을 수 있다. 매니저마다 두면 씬에 붙기 전과 떨어진 뒤의 컴포넌트를 위해 값을 옮겨야 하고, 옮기는 순간 번호가 바뀐다.
     *
     *          언리얼의 `USceneComponent` 는 컴포넌트가 값을 직접 가진다(대량 이동은 ISM 과 Mass 로 우회한다). 게임
     *          오브젝트 모델은 그대로 두고 트랜스폼만 연속 배열로 옮기는 것은 유니티 쪽 구조이고, 회전 변환 캐시(`_arrRotationQuat`)는
     *          언리얼의 `FRotationConversionCache` 를 따랐다. 부모만 움직여 자식을 다시 합성할 때 삼각 함수 여섯 번을 건너뛴다.
     *
     *          스레드 계약: 슬롯 받기와 반환은 뮤텍스 안에서 한다(씬 로드 워커와 게임 스레드가 동시에 만들 수 있다). 페이지 테이블은 고정 크기이고
     *          페이지 포인터는 release 로 발행하므로 `findPage` 는 잠금이 없다. 슬롯 **내용**의 동기화는 쓰는 쪽(씬의 틱 단계)이 맡는다.
     */
    class SW_API SceneTransformStorage
    {
    public:
        /** @brief 슬롯이 없음을 나타냅니다. */
        static constexpr uint32 kInvalidSlot = invalid_index::kUint32;
        /** @brief 슬롯에 렌더 프리미티브가 없음을 나타냅니다(`SceneTransformPage::_arrPrimitiveIndex`). */
        static constexpr uint32 kNoPrimitive = invalid_index::kUint32;
        /**
         * @brief 페이지 테이블의 슬롯 수입니다. 페이지 16384 × 슬롯 256 = 씬 컴포넌트 약 400만 개까지입니다.
         * @details 오브젝트 테이블(`GameObjectManager` 의 id 테이블)이 약 100만 개에서 끝나므로 오브젝트마다 씬 컴포넌트가 네 개를 넘어야 닿습니다.
         */
        static constexpr uint32 kMaxPageCount = 16384;

        /** @brief 전역 저장소입니다. 처음 부를 때 만들어지고 엔진 수명 범위가 닫힐 때(엔진 종료) 사라집니다. 그 뒤에 부르면 멈춥니다. */
        static SceneTransformStorage& get();

        /**
         * @brief 빈 슬롯 하나를 받아 기본값(항등 로컬, 항등 월드, 소유자 알림 켬)으로 채웁니다. 어느 스레드에서 불러도 됩니다.
         * @param pOwner  슬롯을 쓸 컴포넌트
         * @param pOutPage 슬롯이 들어 있는 페이지. 컴포넌트가 보관하고 있다가 테이블 없이 찾습니다
         * @return 슬롯 번호
         */
        uint32 allocateSlot( SceneComponent* pOwner, SceneTransformPage*& pOutPage );
        /** @brief 슬롯을 반환합니다. 어느 스레드에서 불러도 됩니다. `kInvalidSlot` 이면 아무것도 하지 않습니다. */
        void freeSlot( uint32 slot );

        /** @brief 슬롯이 들어 있는 페이지를 반환합니다. 잠금이 없습니다. 받은 적 없는 번호면 nullptr 일 수 있습니다. */
        SceneTransformPage* findPage( uint32 slot ) const
        {
            const uint32 pageNumber = slot >> SceneTransformPage::kSlotShift;
            return pageNumber < kMaxPageCount ? _arrPage[pageNumber].load( std::memory_order_acquire ) : nullptr;
        }
        /** @brief 지금 쓰이는 슬롯 수입니다(테스트와 진단용). */
        uint32 getLiveSlotCount() const;

        /**
         * @brief 슬롯의 로컬 값으로 월드 행렬과 LWC 를 합성합니다. 부모가 없으면 두 부모 포인터를 nullptr 로 줍니다.
         * @details 합성은 이 함수 한 곳에서 한다(컴포넌트의 `updateWorldTransformFromParent`, 틱 뒤 적용, 플러시). 회전이 0 이면 회전
         *          행렬을 건너뛰고, 아니면 슬롯의 쿼터니언 캐시를 쓴다. 오일러가 캐시를 만든 값과 다를 때만 삼각 함수로 다시 만든다.
         *          행렬 곱 순서는 DirectX 행-벡터 규격(Scale * Rotation * Translation, 그다음 부모)이다.
         */
        static void composeWorld( SceneTransformPage& page, uint32 pageIndex, const float4x4* pParentWorld, const double3* pParentLwc );

        SceneTransformStorage( const SceneTransformStorage& )            = delete;
        SceneTransformStorage& operator=( const SceneTransformStorage& ) = delete;

        /** @brief 페이지를 모두 해제합니다. 살아 있는 슬롯이 남았으면 경고합니다(컴포넌트가 새었다). 엔진 수명 범위가 닫힐 때 불립니다. */
        ~SceneTransformStorage();

    private:
        friend class ScopeOwned<SceneTransformStorage>; ///< 엔진 수명 범위가 만들고 소멸시킨다(SceneTransformStorage.cpp)

        /** @brief 빈 저장소를 만듭니다. `get` 만 부릅니다. */
        SceneTransformStorage();

        /** @brief 슬롯 하나를 기본값으로 채웁니다. */
        static void initializeSlot( SceneTransformPage& page, uint32 pageIndex, SceneComponent* pOwner );

        /** @brief 페이지 테이블입니다. 한 번 발행한 페이지는 저장소가 소멸할 때까지 그 위치에 있습니다. */
        atomic<SceneTransformPage*> _arrPage[kMaxPageCount];
        /** @brief 반환된 슬롯입니다. 나중에 반환된 것부터 다시 씁니다. */
        vector<uint32> _listFreeSlot SW_GUARDED_BY( _mutex );
        /** @brief 한 번도 쓰지 않은 슬롯의 첫 번호입니다. */
        uint32 _nextSlot SW_GUARDED_BY( _mutex );
        /** @brief 지금 쓰이는 슬롯 수입니다. */
        uint32 _liveSlotCount SW_GUARDED_BY( _mutex );
        /** @brief 슬롯 받기, 반환, 페이지 만들기를 보호합니다. */
        mutable mutex _mutex;
    };
} // namespace sw
