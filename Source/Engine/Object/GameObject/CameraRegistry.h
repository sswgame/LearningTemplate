/**
 * @file CameraRegistry.h
 * @brief 카메라 컴포넌트의 등록부입니다. 역할 · 우선순위로 카메라를 고르는 규칙도 여기 하나입니다.
 *
 * [왜 필요한가]
 * `LightRegistry` 와 같은 이유입니다. **찾지 말고 등록받습니다.** 게임 카메라(`Scene`)와 에디터 카메라(`EditorCamera`)가 같은
 * 규칙 하나(`selectCamera`)로 고릅니다. 모든 GameObject 를 돌며 `getComponent<CameraComponent>()` 를 물으면 에디터 카메라
 * 조회만으로 프레임마다 세 번(뷰포트 update · draw, 게임 스레드의 뷰 카메라) 씬 전체를 훑고, 한 번 골라 캐시하면 나중에 생긴
 * 더 높은 우선순위의 카메라나 꺼진 카메라를 따라가지 못합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/RegistrationList.h"
#include "Core/Container/vector.h"

namespace sw
{
    enum class CameraRole : uint8;

    class CameraComponent;

    /**
     * @class CameraRegistry
     * @brief 등록된 카메라 목록과 선택 규칙입니다.
     * @note 락은 `LightRegistry` 와 같이 가장 안쪽입니다. 목록은 공통 등록 목록(`RegistrationList`)이라 등록 순서를 지킵니다(순서를 지키며
     *       뺍니다). 선택 규칙은 그 순서에 기대지 않습니다 — 우선순위가 같으면 컴포넌트 id 가 큰(나중에 만든) 카메라입니다.
     */
    class SW_API CameraRegistry
    {
    public:
        /** @brief 빈 등록부를 만듭니다. */
        CameraRegistry() = default;
        /** @brief 등록부를 비웁니다. 카메라의 수명은 GameObject 가 쥡니다. */
        ~CameraRegistry() = default;

        CameraRegistry( const CameraRegistry& )            = delete;
        CameraRegistry& operator=( const CameraRegistry& ) = delete;

        /** @brief 카메라를 등록합니다. 붙을 때 한 번 부릅니다. 이미 등록됐거나 nullptr 이면 무시합니다. */
        void add( CameraComponent* pCamera );
        /** @brief 카메라를 등록 해제합니다. 멱등입니다. */
        void remove( CameraComponent* pCamera );

        /** @brief 등록된 카메라 목록(등록 순서)입니다. 소유하지 않습니다. 활성 판정은 부르는 쪽이 합니다. */
        const vector<CameraComponent*>& getAll() const { return _registeredCamera.getItems(); }

        /**
         * @brief 역할 @p role 의 켜진 카메라 중 우선순위가 가장 높은 것입니다. 같으면 컴포넌트 id 가 큰(나중에 만든) 것이 이깁니다 — 되돌리기 ·
         *        플레이 종료 복원이 id 를 되살리므로 편집 이력과 무관합니다. 없으면 nullptr 입니다.
         * @details 게임 카메라(`Scene::ensureDefaultCameras`)와 에디터 카메라(`EditorCamera::find`)가 함께 쓰는 **하나의** 규칙입니다.
         *          켜짐은 `isUsableCamera` 입니다. 출력이 주 시점이 아닌 카메라(화면 사각형 · 렌더 텍스처)는 고르지 않습니다 — 그것들은 자기 출력의 추가 뷰다.
         */
        CameraComponent* selectCamera( CameraRole role ) const;

        /** @brief 선택 대상이 될 수 있는 카메라인지 봅니다 — 켜져 있고(소유 오브젝트의 계층 활성 포함) 컴포넌트 · 소유자 모두 삭제 대기가 아닙니다. */
        static bool isUsableCamera( const CameraComponent* pCamera );

    private:
        /** @brief 등록된 카메라입니다(등록 순서). 소유하지 않습니다. */
        RegistrationList<CameraComponent> _registeredCamera;
        /** @brief 목록을 지킵니다. 등록/해제는 드물고(비동기 씬 로드는 워커에서 등록합니다), 조회는 게임 스레드입니다. */
        mutable mutex _mutex;
    };
} // namespace sw
