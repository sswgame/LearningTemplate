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
        if ( pCamera == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        if ( std::find( _listCamera.begin(), _listCamera.end(), pCamera ) != _listCamera.end() )
            return;
        _listCamera.push_back( pCamera );
    }

    void CameraRegistry::remove( CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;

        // 순서를 지키며 뺀다 — 우선순위가 같을 때 "뒤에 등록된 것" 이 이기는 규칙이 이 순서에 기댄다. 카메라는 몇 개뿐이다.
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = std::find( _listCamera.begin(), _listCamera.end(), pCamera );
        if ( it != _listCamera.end() )
            _listCamera.erase( it );
    }

    bool CameraRegistry::isUsableCamera( const CameraComponent* pCamera )
    {
        if ( pCamera == nullptr || pCamera->isPendingKill() || pCamera->isActive() == false )
            return false;
        const GameObject* pOwner = pCamera->getOwner();
        return pOwner != nullptr && pOwner->isPendingKill() == false;
    }

    CameraComponent* CameraRegistry::selectCamera( CameraRole role ) const
    {
        int32            bestPriority = MathUtil::MinInt32;
        CameraComponent* pBest{ nullptr };
        for ( CameraComponent* pCamera : _listCamera )
        {
            if ( isUsableCamera( pCamera ) == false || pCamera->getRole() != role )
                continue;
            // `>=` — 우선순위가 같으면 뒤의 것이 이긴다(예전 두 벌의 씬 훑기와 같은 규칙).
            if ( pCamera->getPriority() < bestPriority )
                continue;
            bestPriority = pCamera->getPriority();
            pBest        = pCamera;
        }
        return pBest;
    }
} // namespace sw
