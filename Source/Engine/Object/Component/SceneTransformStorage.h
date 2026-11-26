/**
 * @file SceneTransformStorage.h
 * @brief 씬 컴포넌트 트랜스폼 값(로컬 TRS, 월드 행렬, LWC)의 전역 저장소입니다. 값마다 연속 배열이고, 컴포넌트는 슬롯 번호와 페이지만 가집니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Object/Component/ComponentDataStore.h"
#include "Engine/Object/Component/SceneTransformPage.h"

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
    class SW_API SceneTransformStorage final : public Subsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "SceneTransformStorage";

        /**
         * @brief 페이지 표의 칸 수입니다. 페이지 16384 × 슬롯 256 = 씬 컴포넌트 약 400만 개까지입니다.
         * @details 오브젝트 테이블(`GameObjectManager` 의 id 테이블)이 약 100만 개에서 끝나므로 오브젝트마다 씬 컴포넌트가 네 개를 넘어야 닿습니다.
         */
        static constexpr uint32 kMaxPageCount = 16384;
        /** @brief 칸 저장소입니다. */
        using Store = ComponentDataStore<SceneTransformPage, kMaxPageCount>;

        /** @brief 슬롯이 없음을 나타냅니다. */
        static constexpr uint32 kInvalidSlot = Store::kInvalidSlot;
        /** @brief 슬롯에 렌더 프리미티브가 없음을 나타냅니다(`SceneTransformPage::_arrPrimitiveIndex`). */
        static constexpr uint32 kNoPrimitive = SceneTransformPage::kNoPrimitive;

        /**
         * @brief 빈 저장소를 만듭니다. 서비스 범위가 하나 들고(등록 줄 — `EngineSubsystemRegistration.h`), 컴포넌트 · 계층 · 렌더 수집은 `engine::getSubsystem<SceneTransformStorage>()` 로 찾는다.
         * @details 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 서비스 범위는 씬(엔진 범위)보다 먼저 서고 나중에 내려가 모든 컴포넌트보다 오래 산다
         *          (언리얼 `FSceneInterface` 의 트랜스폼 버퍼가 엔진 쪽 객체에 있는 자리).
         */
        SceneTransformStorage();

        // Subsystem — 세울 것이 없다(처음 받는 칸부터 페이지를 만든다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 빈 슬롯 하나를 받아 기본값으로 채웁니다(`SceneTransformPage::initializeSlot`). 어느 스레드에서 불러도 됩니다. @p pOutPage 는 컴포넌트가 들고 있다가 표 없이 찾는다. */
        uint32 allocateSlot( SceneComponent* pOwner, SceneTransformPage*& pOutPage );
        /** @brief 슬롯을 반환합니다. 어느 스레드에서 불러도 됩니다. `kInvalidSlot` 이면 아무것도 하지 않습니다. */
        void freeSlot( uint32 slot );
        /** @brief 슬롯이 들어 있는 페이지를 반환합니다. 잠금이 없습니다. 받은 적 없는 번호면 nullptr 일 수 있습니다. */
        SceneTransformPage* findPage( uint32 slot ) const { return _store.findPage( slot ); }
        /** @brief 지금 쓰이는 슬롯 수입니다(테스트와 진단용). */
        uint32 getLiveSlotCount() const { return _store.getLiveSlotCount(); }

        /**
         * @brief 슬롯의 로컬 값으로 월드 행렬과 LWC 를 합성합니다. 부모가 없으면 두 부모 포인터를 nullptr 로 줍니다.
         * @details 합성은 이 함수 한 곳에서 한다(컴포넌트의 `updateWorldTransformFromParent`, 틱 뒤 적용, 플러시). 회전이 0 이면 회전
         *          행렬을 건너뛰고, 아니면 슬롯의 쿼터니언 캐시를 쓴다. 오일러가 캐시를 만든 값과 다를 때만 삼각 함수로 다시 만든다.
         *          행렬 곱 순서는 DirectX 행-벡터 규격(Scale * Rotation * Translation, 그다음 부모)이다.
         */
        static void composeWorld( SceneTransformPage& page, uint32 pageIndex, const float4x4* pParentWorld, const double3* pParentLwc );

        SceneTransformStorage( const SceneTransformStorage& )            = delete;
        SceneTransformStorage& operator=( const SceneTransformStorage& ) = delete;

        /** @brief 페이지를 모두 해제합니다. 살아 있는 슬롯이 남았으면 경고합니다(컴포넌트가 새었다). 서비스 범위가 내려갈 때 불립니다. */
        ~SceneTransformStorage() override;

    private:
        Store _store; ///< 칸 받기 · 놓기는 .cpp 에서만 인스턴스화한다(페이지의 칸 채우기 · 비우기가 엔진 모듈 밖에 보이지 않는다)

        SW_SUBSYSTEM_SLOT( SceneTransformStorage, Required );
    };
} // namespace sw
