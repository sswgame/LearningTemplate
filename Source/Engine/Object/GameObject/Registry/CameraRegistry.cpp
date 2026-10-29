/**
 * @file CameraRegistry.cpp
 * @brief 카메라 레지스트리 구현입니다(등록 · 해제 · 역할별 선택).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Registry/CameraRegistry.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    Subscription CameraRegistry::add( CameraComponent* pCamera )
    {
        ScopedLock<mutex> lock{ _mutex };
        // nullptr · 이미 등록된 카메라는 목록이 거절한다(빈 구독) — 먼저 받은 구독이 쥔다.
        Subscription subscription = _registeredCamera.addSubscribed( pCamera, "Camera" );
        if ( subscription.isActive() )
            _registrySerial.fetch_add( 1, std::memory_order_relaxed );
        return subscription;
    }

    void CameraRegistry::releaseCamera( CameraComponent* pCamera )
    {
        // 순서를 지키며 뺀다(목록을 보여 주는 쪽 — 프러스텀 시각화 · 벤치 — 이 순서를 본다). 카메라는 몇 개뿐이다.
        ScopedLock<mutex> lock{ _mutex };
        if ( _registeredCamera.remove( pCamera ) )
            _registrySerial.fetch_add( 1, std::memory_order_relaxed );
        if ( _pCutCamera == pCamera )
            _pCutCamera = nullptr;
    }

    void CameraRegistry::setCutCamera( CameraComponent* pCamera )
    {
        ScopedLock<mutex> lock{ _mutex };
        _pCutCamera = pCamera;
    }

    CameraComponent* CameraRegistry::getCutCamera() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _pCutCamera;
    }

    bool CameraRegistry::isUsableCamera( const CameraComponent* pCamera )
    {
        // 소유 오브젝트의 삭제 대기는 다시 보지 않는다. 그 오브젝트의 컴포넌트는 항상 삭제 대기다(`GameObject::isPendingDestroy`).
        const bool bUnusableCamera = pCamera == nullptr || pCamera->isPendingDestroy() || pCamera->isActive() == false;
        return bUnusableCamera == false && pCamera->getOwner() != nullptr;
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
                // 카메라를 다시 등록할 때마다 순서가 바뀌어 활성 카메라가 편집 이력에 따라 뒤집힌다. id 는 그 복원이 복원한다.
                if ( priority == bestPriority && pCamera->getComponentID() < pBest->getComponentID() )
                    continue;
            }
            bestPriority = priority;
            pBest        = pCamera;
        }
        return pBest;
    }
} // namespace sw
