/**
 * @file CameraRegistry.cpp
 * @brief 카메라 등록부 구현입니다(등록 · 해제 · 역할별 선택).
 */
#include "pch.h"

#include "Engine/Object/GameObject/CameraRegistry.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    void CameraRegistry::add( CameraComponent* pCamera )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        // nullptr · 이미 등록된 카메라는 목록이 거절한다(멱등).
        (void)_registeredCamera.add( pCamera );
    }

    void CameraRegistry::remove( CameraComponent* pCamera )
    {
        // 순서를 지키며 뺀다(목록을 보여 주는 쪽 — 프러스텀 시각화 · 벤치 — 이 순서를 본다). 카메라는 몇 개뿐이다.
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_registeredCamera.remove( pCamera ); // 두 번 빼도 된다 — 없으면 할 일이 없다
        if ( _pCutCamera == pCamera )
            _pCutCamera = nullptr;
    }

    void CameraRegistry::setCutCamera( CameraComponent* pCamera )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _pCutCamera = pCamera;
    }

    CameraComponent* CameraRegistry::getCutCamera() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _pCutCamera;
    }

    bool CameraRegistry::isUsableCamera( const CameraComponent* pCamera )
    {
        if ( pCamera == nullptr || pCamera->isPendingDestroy() || pCamera->isActive() == false )
            return false;
        const GameObject* pOwner = pCamera->getOwner();
        return pOwner != nullptr && pOwner->isPendingDestroy() == false;
    }

    CameraComponent* CameraRegistry::selectCamera( CameraRole role ) const
    {
        int32            bestPriority = MathUtil::kMinInt32;
        CameraComponent* pBest{ nullptr };
        for ( CameraComponent* pCamera : _registeredCamera.getItems() )
        {
            if ( isUsableCamera( pCamera ) == false || pCamera->getRole() != role )
                continue;
            // 화면 사각형 · 렌더 텍스처로 가는 카메라는 주 시점이 아니다(분할 화면의 둘째 플레이어 · PiP · CCTV) — 자기 출력으로만 그린다.
            if ( pCamera->getRenderOutput()._target != CameraOutputTarget::MainView )
                continue;
            const int32 priority = pCamera->getPriority();
            if ( pBest != nullptr )
            {
                if ( priority < bestPriority )
                    continue;
                // 우선순위가 같으면 **컴포넌트 id 가 큰 쪽**(나중에 만든 쪽)이 이긴다. 등록 순서로 가르면 되돌리기 · 플레이 종료 복원이
                // 카메라를 다시 등록할 때마다 순서가 바뀌어 활성 카메라가 편집 이력에 따라 뒤집힌다. id 는 그 복원이 되살린다.
                if ( priority == bestPriority && pCamera->getComponentID() < pBest->getComponentID() )
                    continue;
            }
            bestPriority = priority;
            pBest        = pCamera;
        }
        return pBest;
    }
} // namespace sw
