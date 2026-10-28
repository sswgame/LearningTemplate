/**
 * @file CameraRegistry.h
 * @brief 카메라 컴포넌트의 레지스트리입니다. 역할 · 우선순위로 카메라를 고르는 규칙도 여기 하나입니다.
 *
 * [왜 필요한가]
 * `ComponentRegistry` 와 같은 이유입니다. **찾지 말고 등록받습니다.** 게임 카메라(`Scene`)와 뷰포트 카메라(`CameraRole::Viewport`)가 같은
 * 규칙 하나(`selectCamera`)로 고릅니다. 모든 GameObject 를 돌며 `getComponent<CameraComponent>()` 를 물으면 뷰포트 카메라
 * 조회만으로 프레임마다 세 번(뷰포트 update · draw, 게임 스레드의 뷰 카메라) 씬 전체를 훑고, 한 번 골라 캐시하면 나중에 생긴
 * 더 높은 우선순위의 카메라나 꺼진 카메라를 따라가지 못합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Object/GameObject/Registry/CameraShakeChannel.h"

namespace sw
{
    enum class CameraRole : uint8;

    class CameraComponent;

    /**
     * @class CameraRegistry
     * @brief 등록된 카메라 목록과 선택 규칙입니다.
     * @note 락은 `ComponentRegistry` 와 같이 가장 안쪽입니다. 목록은 공통 등록 목록(`RegistrationList`)이라 등록 순서를 지킵니다(순서를 지키며
     *       뺍니다). 선택 규칙은 그 순서에 기대지 않습니다 — 우선순위가 같으면 컴포넌트 id 가 큰(나중에 만든) 카메라입니다.
     */
    class SW_API CameraRegistry
    {
    public:
        /** @brief 빈 레지스트리를 만듭니다. */
        CameraRegistry() = default;
        /** @brief 레지스트리를 비웁니다. 카메라의 수명은 GameObject 가 쥡니다. */
        ~CameraRegistry() = default;

        CameraRegistry( const CameraRegistry& )            = delete;
        CameraRegistry& operator=( const CameraRegistry& ) = delete;

        /** @brief 카메라를 등록하고 그 구독을 돌려줍니다. 붙을 때 한 번 부르고, 구독이 사라지면 빠집니다. 이미 등록됐거나 nullptr 이면 빈 구독입니다. */
        [[nodiscard]] Subscription add( CameraComponent* pCamera );

        /** @brief 등록된 카메라 목록(등록 순서)입니다. 소유하지 않습니다. 활성 판정은 부르는 쪽이 합니다. */
        const vector<CameraComponent*>& getAll() const { return _registeredCamera.getItems(); }
        /**
         * @brief 등록 · 해제 · 선택 · 맞춤 입력(역할 · 우선순위 · 출력 대상 · 시야각) 변경 때마다 오르는 변경 일련번호입니다(`GameObjectManager::getUpdateSerial` 과 같은 방식). 읽는 쪽이 마지막 값과 비교해 카메라 집합이 바뀌었는지 압니다.
         * @details 한 프레임에 하나가 빠지고 다른 하나가 들어오는 교체도 번호가 올라 수 비교가 놓치는 경우를 잡습니다. 오름은 락 안에서, 읽기는 락 없이(어느 스레드든) 합니다. 넘치면 돌아갑니다.
         * @note 올리지 않는 것: `setCutCamera`(게임 카메라 고르기만 바꾸고 집합 · 선택 입력은 그대로), 뷰포트 크기 · 픽셀 스냅 단위(집합과 무관), 직교 전환 · 직교 높이 · 종횡비 · 근 · 원 평면(벤치 맞춤은 시야각만 읽는다. 직교 높이는 에디터 직교 뷰의 줌마다 바뀌어 번호에 넣으면 사용자 조작을 벤치가 덮고, 원 평면은 맞춤이 스스로 키운다), 켜짐 전환(`isUsableCamera` 는 조회할 때마다 평가하고 캐시가 없다).
         */
        uint32 getRegistrySerial() const { return _registrySerial.load( std::memory_order_relaxed ); }
        /** @brief 선택 규칙과 맞춤의 입력(역할 · 우선순위 · 출력 대상 · 시야각)이 바뀌었음을 알려 번호를 올립니다. 카메라 컴포넌트의 세터가 값이 실제로 바뀔 때만 부릅니다. */
        void noteChanged() { _registrySerial.fetch_add( 1, std::memory_order_relaxed ); }

        /**
         * @brief 역할 @p role 의 켜진 카메라 중 우선순위가 가장 높은 것입니다. 같으면 컴포넌트 id 가 큰(나중에 만든) 것이 이깁니다 — 되돌리기 ·
         *        플레이 종료 복원이 id 를 복원하므로 편집 이력과 무관합니다. 없으면 nullptr 입니다.
         * @details 게임 카메라(`Scene::pickActiveGameCamera`)와 도구의 뷰포트 카메라(`CameraRole::Viewport`)가 함께 쓰는 **하나의** 규칙입니다.
         *          켜짐은 `isUsableCamera` 입니다. 출력이 주 시점이 아닌 카메라(화면 사각형 · 렌더 텍스처)는 고르지 않습니다 — 그것들은 자기 출력의 추가 뷰다.
         */
        CameraComponent* selectCamera( CameraRole role ) const;

        /**
         * @brief 시퀀서 카메라 컷이 고른 카메라를 적습니다(nullptr 이면 풉니다). 게임 카메라 선택에서 직접 고른 카메라 · 레지스트리 규칙보다 먼저입니다.
         * @details 언리얼 Camera Cut 트랙이 뷰 타깃을 덮는 자리입니다. 그 카메라의 등록 구독이 풀리면 함께 풀립니다.
         */
        void setCutCamera( CameraComponent* pCamera );
        /** @brief 카메라 컷이 고른 카메라입니다. 없으면 nullptr 입니다. */
        CameraComponent* getCutCamera() const;

        /**
         * @brief 이 씬의 게임 카메라가 그리는 뷰포트의 픽셀 크기를 적습니다(`Engine` 가 틱 전에). 모르면 0 입니다.
         * @details 주 출력(에디터 게임 뷰의 이미지)이 있으면 그 크기, 없으면 백버퍼다 — 창 크기가 아니다. 카메라의 종횡비(`CameraPoseUtil::computeAspectRatio`)와
         *          픽셀 퍼펙트 배율이 이 값을 읽는다.
         */
        void setViewportSize( uint32 width, uint32 height )
        {
            _viewportWidth  = width;
            _viewportHeight = height;
        }
        /** @brief 게임 뷰포트의 픽셀 폭입니다(모르면 0). */
        uint32 getViewportWidth() const { return _viewportWidth; }
        /** @brief 게임 뷰포트의 픽셀 높이입니다(모르면 0). */
        uint32 getViewportHeight() const { return _viewportHeight; }
        /**
         * @brief 2D 픽셀 스냅 단위(자산 픽셀 하나의 월드 길이 = 1 / PPU)입니다. 0 이면 끕니다.
         * @details 픽셀 퍼펙트 카메라가 정하고, 새로 등록되는 메시 컴포넌트가 읽습니다(`MeshComponent::onRegister`). 바뀔 때 이미 있는 메시에 알리는
         *          것은 정하는 쪽의 일입니다(`PixelPerfectCameraComponent`).
         */
        float32 getPixelSnapUnit() const { return _pixelSnapUnit; }
        void    setPixelSnapUnit( float32 unit ) { _pixelSnapUnit = unit; }

        /**
         * @brief 이 씬의 카메라 흔들림 요청 채널입니다. 애니메이션 알림(`CameraShake`)이 내고 카메라 매니저가 플레이 동안 구독합니다.
         * @details 씬이 소유하므로 다른 씬의 카메라는 받지 않고, 씬이 사라지면 남은 구독은 무효가 됩니다(언리얼 `PlayerCameraManager` 가 월드에 속하는 것과 같다).
         */
        CameraShakeChannel& getShakeChannel() { return _shakeChannel; }

        /** @brief 선택 대상이 될 수 있는 카메라인지 봅니다 — 켜져 있고(소유 오브젝트의 계층 활성 포함) 컴포넌트 · 소유자 모두 삭제 대기가 아닙니다. */
        static bool isUsableCamera( const CameraComponent* pCamera );

    private:
        /** @brief 카메라 등록 구독이 풀렸습니다(@p id 는 카메라 주소). 순서를 지키며 빼고 컷 카메라였으면 풉니다. */
        void releaseSubscription( uint64 id );

        /** @brief 등록된 카메라입니다(등록 순서). 소유하지 않습니다. 카메라는 뷰마다 하나(화면 분할 · CCTV · 미리보기)라 많아야 수십 개이므로 선형 등록 목록으로 충분합니다. */
        RegistrationList<CameraComponent> _registeredCamera{ "CameraRegistry.Camera" };
        /** @brief 목록을 지킵니다. 등록/해제는 드물고(비동기 씬 로드는 워커에서 등록합니다), 조회는 게임 스레드입니다. */
        mutable mutex      _mutex;
        atomic<uint32>     _registrySerial{ 0 };   ///< 등록 · 해제 때마다 오릅니다(`getRegistrySerial`)
        CameraComponent*   _pCutCamera{ nullptr }; ///< 시퀀서 카메라 컷(`setCutCamera`). 등록 해제가 함께 푼다
        uint32             _viewportWidth{ 0 };    ///< 게임 뷰포트 픽셀 폭(게임 스레드가 틱 전에 적고 틱이 읽습니다)
        uint32             _viewportHeight{ 0 };   ///< 게임 뷰포트 픽셀 높이
        float32            _pixelSnapUnit{ 0.0f }; ///< 2D 픽셀 스냅 단위(0 = 끔)
        CameraShakeChannel _shakeChannel;          ///< 흔들림 요청 채널(`getShakeChannel`). 자기 잠금을 쓴다

    private:
        /** @brief 이 객체가 내준 구독입니다(믹스인 상속 대신 합성). 풀리면 `releaseSubscription` 이 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&CameraRegistry::releaseSubscription>( this ) };
    };
} // namespace sw
