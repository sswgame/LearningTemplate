/**
 * @file Box2DPhysicsBackend.h
 * @brief 2D 물리 백엔드(Box2D v3)의 입구 — 씬 만들기입니다. 이 헤더는 Box2D 헤더를 include 하지 않습니다.
 * @details Box2D 는 전역 상태가 없어(월드마다 따로) 올리고 내릴 것이 없습니다. 월드는 단일 스레드로 돕니다 — Box2D 의 솔버 태스크는 워커들이
 *          **동시에** 돌며 서로를 바쁘게 기다리므로(스테이지 동기화), 엔진의 공유 작업 풀(다른 일로 바쁜 워커가 있을 수 있다)에 그대로 넘기면
 *          교착할 수 있습니다. 2D 장면은 바디가 적어 한 스레드로 충분합니다. 필요해지면 전용 워커를 붙입니다(`b2WorldDef::enqueueTask`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    struct PhysicsSettings;

    /** @brief Box2D 백엔드의 입구입니다. */
    struct SW_API Box2DPhysicsBackend
    {
        /** @brief 설정(중력 · 레이어 표 · 재질 · 서브 스텝)으로 씬 하나를 만듭니다. */
        static unique_ptr<IPhysicsScene2D> createScene( const PhysicsSettings& settings );
    };
} // namespace sw
