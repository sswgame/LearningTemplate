/**
 * @file EnvironmentUtil.h
 * @brief 지형 · 식생 · 물 컴포넌트가 함께 쓰는 것 — 보는 카메라 위치, 캐시에서 잡은 머티리얼 + 컴포넌트 몫 인스턴스입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class GameObjectManager;
    class Material;
    class MaterialInstance;

    /**
     * @struct EnvironmentUtil
     * @brief 환경 컴포넌트의 공통 도우미입니다.
     */
    struct SW_API EnvironmentUtil
    {
        /**
         * @brief @p manager 의 게임 카메라(없으면 등록된 첫 카메라) 위치입니다. 카메라가 없으면 false 입니다.
         * @details 틱 안에서 읽으면 지난 프레임에 적용된 트랜스폼입니다(트랜스폼 쓰기는 틱 뒤에 적용됩니다) — LOD · 바람 · 거리 페이드에는 충분합니다.
         */
        [[nodiscard]] static bool findViewPosition( GameObjectManager& manager, float3& outPosition );
        /**
         * @brief 환경 시간(바람 · 파도)이 흐르면 true 입니다. `-gv_environmentAnimate=0` 이면 시간이 0 에 멈춥니다 — 백엔드끼리 스크린샷을 비교하는 자리입니다
         *        (벽시계 시간은 실행마다 달라 같은 프레임도 다른 그림이 된다).
         */
        static bool isAnimationEnabled();
    };
} // namespace sw

namespace sw
{
    /**
     * @class EnvironmentMaterial
     * @brief 머티리얼 캐시에서 경로로 잡은 머티리얼과, 컴포넌트가 값을 싣는 자기 몫 인스턴스입니다. 놓기는 `release` 하나입니다.
     * @details 엔진 서비스가 없으면(CPU 시험) 아무것도 잡지 않고 `getMaterial` 이 nullptr 입니다 — 그리는 쪽은 씬 기본 머티리얼로 물러납니다.
     *          인스턴스는 이 객체만 값을 쓰므로 그 컴포넌트의 틱에서 값을 바꿔도 됩니다.
     */
    class SW_API EnvironmentMaterial
    {
    public:
        EnvironmentMaterial();
        ~EnvironmentMaterial();

        EnvironmentMaterial( const EnvironmentMaterial& )            = delete;
        EnvironmentMaterial& operator=( const EnvironmentMaterial& ) = delete;

        /** @brief @p path 의 머티리얼을 잡고 새 인스턴스를 만듭니다. 이미 잡은 것은 먼저 놓습니다. 잡았으면 true 입니다. */
        bool acquire( string_view path );
        /** @brief 인스턴스와 머티리얼을 놓습니다. 멱등입니다. */
        void release();

        Material*                           getMaterial() const { return _pMaterial; }
        const shared_ptr<MaterialInstance>& getInstance() const { return _instance; }

        /** @brief 인스턴스의 float4 값을 씁니다. 인스턴스가 없으면 아무것도 하지 않습니다. */
        void setVector( const hashed_string& name, const float4& value );
        /** @brief 인스턴스의 텍스처를 에셋 경로로 겁니다. 경로가 비었거나 인스턴스가 없으면 아무것도 하지 않습니다. */
        void setTexture( const hashed_string& name, string_view texturePath );

    private:
        shared_ptr<MaterialInstance> _instance;
        Material*                    _pMaterial;
        hashed_string                _acquiredPath;
    };
} // namespace sw
