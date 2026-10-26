/**
 * @file EditorViewportCamera.h
 * @brief 씬 뷰 카메라의 상태(위치 · 회전 · 궤도 · 직교)와 그 입력(비행 · 궤도 · 북마크 · 초점)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class CameraComponent;
} // namespace sw

namespace sw::editor
{
    struct CameraBookmark;

    /**
     * @class EditorViewportCamera
     * @brief 씬 뷰 카메라입니다. 값은 이 객체가 들고, 프레임마다 `applyToCamera` 가 에디터 카메라 컴포넌트에 씁니다.
     * @details `EditorViewportClient` 가 값 멤버로 들고 직접 부릅니다. 외부(개발 명령 bugitgo, teleport)가 컴포넌트를 옮겼으면
     *          그 위치와 시선을 이어받습니다.
     */
    class EditorViewportCamera
    {
    public:
        EditorViewportCamera();

        /** @brief 오른쪽 단추 비행(마우스 회전 + WASD/QE 이동)입니다. 휠은 @p inoutCameraSpeed(툴바 슬라이더와 같은 값)를 바꿉니다. */
        void processFlyInput( float32 deltaTime, float32& inoutCameraSpeed );
        /** @brief Alt 궤도 입력입니다. 왼쪽 단추는 회전, 오른쪽 단추와 휠은 궤도 거리입니다. 궤도 중심은 `processFlyInput` 이 시선 앞으로 이어 둔다. */
        void processOrbitInput();
        /** @brief 원근 보기의 휠 줌입니다. 궤도 중심을 두고 궤도 거리를 거리에 비례해 줄이거나 늘립니다(@p mouseWheel 이 양수면 가까워진다). */
        void dolly( float32 mouseWheel );
        /** @brief 가운데 단추 팬입니다. 마우스 이동 픽셀(@p pixelX, @p pixelY)만큼 화면 평면에서 카메라와 궤도 중심을 같이 옮깁니다. @p viewportHeightPixels 는 뷰포트의 실제 높이이고 0 이하이면 옮기지 않습니다. */
        void pan( float32 pixelX, float32 pixelY, float32 viewportHeightPixels );
        /** @brief 숫자 키 1~9 북마크입니다. Ctrl 과 함께 누르면 저장하고, 수식 키 없이 누르면 불러옵니다. */
        void processBookmarkKeys();

        /** @brief 툴바가 요청한 북마크(@p inoutSlot, 0 이상)를 불러오고 요청을 지웁니다(-1). */
        void applyBookmarkRequest( int32& inoutSlot );
        /** @brief 이 카메라 값을 에디터 카메라 컴포넌트에 씁니다. 그 전에 외부가 컴포넌트를 옮겼으면 그 위치를 이어받습니다. */
        void applyToCamera( CameraComponent* pCamera );

        /** @brief 현재 선택한 GameObject 가 화면에 들어오도록 카메라를 맞춥니다(F 키). */
        void frameSelected();
        /** @brief @p target 을 반지름 @p radius 의 물체처럼 화면에 담도록 현재 방향 그대로 물러납니다. */
        void focusOnPoint( const float3& target, float32 radius );
        /** @brief 직교 보기로 바꿉니다. @p pitch 와 @p yaw 는 도(°) 단위입니다. 지금 보는 점을 그 축 방향에서 다시 봅니다. */
        void setOrthographicView( float32 pitch, float32 yaw );
        /** @brief 직교와 원근을 전환합니다(키패드 5). */
        void toggleOrthographic();
        /** @brief 직교 보기의 화면 높이를 휠 값만큼 줄이거나 늘립니다. */
        void zoomOrthographic( float32 mouseWheel );
        /** @brief 궤도 대상을 중심으로 회전을 @p rotation(도)으로 바꾸고 위치를 맞춥니다(방향 큐브 클릭). */
        void orbitTo( const float3& rotation );
        /** @brief 위치와 회전(도, 피치 · 요 · 롤)을 그대로 놓습니다(개발 명령 `camera.set` — 시나리오가 같은 시점에서 찍는다). 궤도 대상은 시선 앞 궤도 거리입니다. */
        void setPose( const float3& position, const float3& rotation );
        /**
         * @brief 포즈 · 투영을 새 에디터 세션의 값으로 되돌립니다(개발 명령 `camera.reset`). 마지막으로 쓴 카메라는 그대로 기억한다 —
         *        새 객체로 바꾸면 다음 `applyToCamera` 가 그 카메라를 "앞 인스턴스가 놓은 카메라" 로 보고 옛 포즈를 이어받는다.
         */
        void resetToDefault();

        /** @brief 뷰 행렬을 ImGuizmo 배열로 계산합니다. */
        void getViewMatrix( float32* pOutMatrix ) const;
        /** @brief 투영 행렬을 ImGuizmo 배열로 계산합니다. */
        void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const;

        const float3& getPosition() const { return _position; }
        /** @brief 회전(도)입니다. x 는 피치, y 는 요, z 는 롤입니다. */
        const float3& getRotation() const { return _rotation; }
        bool          isOrthographicView() const { return _bOrthographicView == SW_TRUE; }
        /** @brief 시선 앞 궤도 거리(m)입니다. 휠 줌과 초점이 바꾼다. */
        float32 getOrbitDistance() const { return _orbitDistance; }
        /**
         * @brief 지금(또는 막 끝난) 오른쪽 단추 누름 동안 비행 키(WASD · QE)로 카메라를 옮겼는가입니다. 단추를 누르는 프레임에 지워집니다.
         * @details 오른쪽 단추를 놓을 때 여는 맥락 메뉴가 이것과 끌기 거리로 "비행이었다" 를 가린다. 마우스를 끌지 않고 WASD 로만 날아도 비행이다 —
         *          끌기 거리(GetMouseDragDelta)만 보면 키로 난 비행 끝에 메뉴가 열려 다음 클릭을 삼킨다(언리얼 · 유니티도 비행 뒤에는 메뉴를 열지 않는다).
         */
        bool hasFlownThisRightHold() const { return _bFlownThisRightHold == SW_TRUE; }

    private:
        void applyBookmark( const CameraBookmark& bookmark );

    private:
        float3                 _position;
        float3                 _rotation; // Pitch, Yaw, Roll
        float3                 _orbitTarget;
        float3                 _lastAppliedPosition; ///< 지난 프레임에 에디터 카메라에 설정한 로컬 위치. 외부(bugitgo, teleport)가 옮겼는지 구분하는 데 쓴다
        uint64                 _lastAppliedCameraID; ///< 그 카메라 컴포넌트 id. 0 이면 아직 설정하지 않았다. 씬이 바뀌어 새 카메라가 되었으면 받아들이지 않는다
        float32                _orbitDistance;
        float32                _fovY;
        float32                _nearZ;
        float32                _farZ;
        float32                _orthoHeight; ///< 직교 보기의 화면 높이(월드 단위). 휠로 바꾼다
        uint8                  _bOrthographicView   : 1;
        uint8                  _bFlownThisRightHold : 1; ///< 이번 오른쪽 단추 누름 동안 비행이 카메라를 옮기거나 돌렸다
        [[maybe_unused]] uint8 _reserved            : 6;
    };
} // namespace sw::editor
