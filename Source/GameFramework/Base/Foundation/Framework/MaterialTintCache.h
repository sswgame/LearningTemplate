/**
 * @file MaterialTintCache.h
 * @brief 색 하나마다 머티리얼 인스턴스 하나를 만들어 나눠 쓰는 캐시입니다 — 디렉터가 씬이 늘 들고 있는 머티리얼에서 색만 바꾼 모습을 만듭니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Material;
    class MaterialInstance;
    class MeshComponent;

    /** @brief 색을 바꾸는 머티리얼 벡터 매개변수 이름입니다(셰이더 머티리얼 구조체의 `color` 와 묶인 문자열). 내보낸 클래스의 정적 멤버로 두면 지연 로드하는 게임 모듈이 데이터 import 를 못 한다. */
    inline constexpr string_view kMaterialColorParameter = "color";

    /**
     * @class MaterialTintCache
     * @brief (부모 머티리얼 · 색) 하나에 인스턴스 하나입니다. 같은 색은 같은 인스턴스를 나눠 써서 배치 키가 갈라지지 않습니다.
     * @details 색만 다른 머티리얼 에셋을 두면 그 에셋은 처음 스폰할 때 게임 스레드에서 올라가고 마지막 것이 사라질 때 내려가 렌더 스레드의 병렬
     *          기록과 겹친다(`Source/Games/README.md`). 인스턴스는 `"color"` 벡터 매개변수 하나를 바꿉니다. 게임 스레드에서만 씁니다.
     */
    class SW_GF_API MaterialTintCache
    {
    public:
        MaterialTintCache();

        /** @brief @p pMaterial 위에 @p color 를 입힌 인스턴스를 찾거나 만듭니다. 머티리얼이 없거나 만들 수 없으면 nullptr 입니다. */
        shared_ptr<MaterialInstance> acquire( Material* pMaterial, const float4& color );
        /** @brief @p mesh 의 머티리얼에서 @p color 인스턴스를 얻어 메시에 겁니다. 머티리얼이 없으면 메시를 그대로 둡니다. */
        void apply( MeshComponent& mesh, const float4& color );
        /** @brief 든 인스턴스를 모두 놓습니다. */
        void clear() { _listEntry.clear(); }

    private:
        /** @brief 색 하나의 인스턴스입니다. */
        struct Entry
        {
            shared_ptr<MaterialInstance> _instance{};
            float4                       _color{};
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
