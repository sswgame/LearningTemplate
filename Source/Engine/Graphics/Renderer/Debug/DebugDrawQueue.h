/**
 * @file DebugDrawQueue.h
 * @brief CPU 디버그 프리미티브 큐(선 · 구 · 상자 · 화살표 · 글자)입니다. GPU 즉시 드로우가 아닙니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct quaternion;

    /// @brief 디버그 선입니다(시작 · 끝 · 색 · 남은 시간 · 카테고리). 상자 · 화살표도 넣을 때 선으로 펼쳐집니다.
    struct DebugLine
    {
        float3        _from{};
        float3        _to{};
        float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        hashed_string _category{};
        float32       _remainingSeconds{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /// @brief 디버그 구입니다(중심 · 반지름 · 색 · 남은 시간 · 카테고리).
    struct DebugSphere
    {
        float3        _center{};
        float32       _radius{ 1.0f };
        float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        hashed_string _category{};
        float32       _remainingSeconds{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /// @brief 월드 위치에 붙는 디버그 글자입니다.
    struct DebugText
    {
        float3        _position{};
        float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        string        _text{};
        hashed_string _category{};
        float32       _remainingSeconds{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class DebugDrawQueue
     * @brief 디버그 지오메트리 큐입니다. 게임 코드가 채우고 에디터 Game View(`debug_draw` 시각화)가 그립니다.
     * @details - **보이는 목록은 프레임 끝에 확정됩니다**(`endFrame`). 그 프레임에 넣은 것(게임 업데이트 · 씬 틱 어디서든)이 다음 에디터
     *            프레임에 모두 보입니다. 그리는 쪽은 `getVisible*` 만 읽습니다.
     *          - **지속 시간**: 0 이면 한 프레임, 그보다 크면 그 시간(초) 동안 남습니다. 흐르는 시간은 `endFrame` 의 델타입니다 — 씬을 틱하지
     *            않은 프레임(에디터 일시정지)은 0 이라 보이던 것이 그대로 남습니다.
     *          - **카테고리**: 비우면 `Default` 입니다. 끈 카테고리는 보이는 목록에서 빠집니다(남은 시간은 그대로 흐릅니다).
     *          - 넣기는 어느 스레드에서 해도 됩니다(병렬 onTick). `endFrame` · `getVisible*` 는 게임 스레드에서만 부릅니다.
     *          - 2D 는 Z 가 같은 평면의 도형입니다. 반 크기 중 0 인 축이 있는 상자는 사각형 하나(선 넷)로 펼칩니다.
     */
    class SW_API DebugDrawQueue
    {
    public:
        /** @brief 카테고리를 주지 않은 도형의 카테고리 이름입니다. */
        static constexpr const utf8* kDefaultCategoryName = "Default";
        /** @brief 화살표 머리 길이가 화살표 길이에서 차지하는 비율입니다. */
        static constexpr float32 kArrowHeadRatio = 0.2f;

        /** @brief 빈 디버그 드로우 큐로 만듭니다. */
        DebugDrawQueue();

        /** @brief 남은 것 · 보이는 것을 모두 비웁니다(카테고리 켜짐은 그대로). */
        void clear();
        /**
         * @brief 이번 프레임에 넣은 것과 아직 남은 것을 보이는 목록으로 확정하고, 시간을 흘려 끝난 것을 뺍니다. 프레임마다 한 번 부릅니다.
         * @param deltaSeconds 흐른 시간(초). 0 이면(일시정지) 아무것도 빼지 않습니다.
         */
        void endFrame( float32 deltaSeconds );

        /** @brief 선을 예약합니다. */
        void drawLine( const float3& from, const float3& to, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} );
        /** @brief 구를 예약합니다. 2D 뷰에서는 XY 평면의 원 하나로 그려집니다. */
        void drawSphere( const float3& center, float32 radius, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} );
        /** @brief 축 정렬 상자를 선 열둘로 예약합니다. 반 크기 중 0 인 축이 있으면 사각형(선 넷)입니다. */
        void drawBox( const float3& center, const float3& halfExtent, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} );
        /** @brief 회전한 상자를 예약합니다. */
        void drawOrientedBox( const float3& center, const float3& halfExtent, const quaternion& rotation, const float4& color, float32 durationSeconds = 0.0f,
                              const hashed_string& category = {} );
        /**
         * @brief 화살표(몸통 + 머리 선 넷)를 예약합니다.
         * @details 머리의 한 쌍은 XY 평면 안에 있어 2D 뷰에서도 화살촉으로 보입니다. 길이가 0 이면 아무것도 넣지 않습니다.
         */
        void drawArrow( const float3& from, const float3& to, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} );
        /** @brief 월드 위치에 글자를 예약합니다. */
        void drawText( const float3& position, string_view text, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} );

        /** @brief 카테고리를 켜거나 끕니다. 처음 보는 이름이면 목록에 더합니다. */
        void setCategoryEnabled( const hashed_string& category, bool bEnabled );
        /** @brief 카테고리가 켜져 있으면 true 입니다(처음 보는 이름은 켜짐). */
        bool isCategoryEnabled( const hashed_string& category ) const;
        /** @brief 지금까지 본 카테고리 이름을 사전순으로 채웁니다(토글 UI 용). */
        void collectCategories( vector<hashed_string>& outListCategory ) const;

        /** @brief 지난 `endFrame` 이 확정한 선 목록입니다(상자 · 화살표가 펼친 선 포함). */
        const vector<DebugLine>& getVisibleLines() const { return _listVisibleLine; }
        /** @brief 지난 `endFrame` 이 확정한 구 목록입니다. */
        const vector<DebugSphere>& getVisibleSpheres() const { return _listVisibleSphere; }
        /** @brief 지난 `endFrame` 이 확정한 글자 목록입니다. */
        const vector<DebugText>& getVisibleTexts() const { return _listVisibleText; }

    private:
        /** @brief 빈 카테고리를 기본 이름으로 바꾸고 목록에 올립니다. 잠근 채 부릅니다. */
        hashed_string registerCategoryLocked( const hashed_string& category );
        /** @brief 선 하나를 잠근 채 넣습니다. 길이가 0 인 선은 넣지 않습니다. */
        void pushLineLocked( const float3& from, const float3& to, const float4& color, float32 durationSeconds, const hashed_string& category );
        /** @brief 잠근 채 카테고리 켜짐을 읽습니다. */
        bool isCategoryEnabledLocked( const hashed_string& category ) const;

    private:
        mutable mutex                       _mutex;
        vector<DebugLine>                   _listLine;
        vector<DebugSphere>                 _listSphere;
        vector<DebugText>                   _listText;
        vector<DebugLine>                   _listVisibleLine;
        vector<DebugSphere>                 _listVisibleSphere;
        vector<DebugText>                   _listVisibleText;
        unordered_map<hashed_string, uint8> _mapCategoryEnabled; ///< 본 카테고리 → 켜짐(1) · 꺼짐(0)
    };
} // namespace sw
