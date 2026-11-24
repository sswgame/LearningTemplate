/**
 * @file PrimitiveMarkerBatch.h
 * @brief 판의 상태를 원시 도형 메시로 프레임마다 그리는 표식 모둠입니다. 뷰 컴포넌트가 `begin` → `draw` … → `end` 로 쓰고, 표식 오브젝트는 앞 프레임의 것을 다시 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/WorldOrigin.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class PrimitiveMarkerBatch
     * @brief 메시 하나짜리 프리팹의 인스턴스들입니다. 자리는 절대로 받아 그릴 때 씬 자리로 옮깁니다(형식이 강제한다).
     * @details 디버그 그리기는 게임 화면에 나오지 않으므로(게임 뷰 토글을 켠 개발 실행만) 판을 보이는 뷰는 이것을 쓴다. 표식은 상태에서 다시 세우는 파생이라 저장에 싣지 않는다
     *          (`GameObject::setTransient`). 게임 스레드에서(틱 안이면 틱 뒤) 부른다.
     */
    class SW_GF_API PrimitiveMarkerBatch
    {
    public:
        PrimitiveMarkerBatch();

        /** @brief 표식 프리팹(루트에 `MeshComponent`)의 경로를 정합니다. */
        void setPrefab( string_view prefabPath );
        /** @brief 이번 프레임을 시작합니다. */
        void begin() { _usedCount = 0; }
        /** @brief 표식 하나를 @p center 에 @p size(가로 · 세로 · 깊이)로 그립니다. 프리팹을 세우지 못하면 처음 한 번 오류를 남기고 그리지 않습니다. */
        void draw( GameObjectManager& manager, const AbsolutePosition& center, const float3& size, const float4& color );
        /** @brief 바닥 자리 @p foot 에 선 표식 하나입니다(가운데를 높이의 절반만큼 올려 그린다). */
        void drawStanding( GameObjectManager& manager, const AbsolutePosition& foot, const float3& size, const float4& color ) { draw( manager, foot + float3{ 0.0f, size._y * 0.5f, 0.0f }, size, color ); }
        /** @brief 이번 프레임에 쓰지 않은 표식을 숨깁니다. */
        void end( GameObjectManager& manager );
        /** @brief 세운 표식을 모두 지웁니다(플레이 끝). */
        void destroyAll( GameObjectManager& manager );

    private:
        /** @brief 표식 하나입니다. 색은 바뀔 때만 다시 입힌다. */
        struct Marker
        {
            GameObjectHandle       _object;
            float4                 _color;
            uint8                  _bColored : 1; ///< 지금 색을 입혔다
            [[maybe_unused]] uint8 _reserved : 7;

            Marker()
                : _object{}
                , _color{ 0.0f, 0.0f, 0.0f, 0.0f }
                , _bColored{ SW_FALSE }
                , _reserved{ 0 }
            {
            }
        };

        /** @brief 프리팹을 세웁니다. 세우지 못하면 오류를 남기고 다시 세우지 않으며 nullptr 입니다. */
        GameObject* spawnMarker( GameObjectManager& manager );

    private:
        string                 _prefabPath;
        vector<Marker>         _listMarker;
        MaterialTintCache      _tintCache;
        size_t                 _usedCount;
        uint8                  _bRefused : 1; ///< 프리팹을 세우지 못했다 — 프레임마다 오류를 되풀이하지 않는다
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
