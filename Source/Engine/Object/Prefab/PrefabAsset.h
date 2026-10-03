/**
 * @file PrefabAsset.h
 * @brief 프리팹 에셋의 로드 · 저장 · 스폰입니다(GameObject 템플릿: XML · JSON · PFB2 바이너리).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw
{
    struct ObjectIdentity;

    class GameObject;
    class GameObjectManager;

    /// @brief 프리팹 상태 본문의 형식입니다. 읽을 때 한 번 정하고, 상태를 쓰는 쪽은 이것을 봅니다(본문 첫 글자로 다시 짐작하지 않습니다).
    enum class PrefabStateFormat : uint8
    {
        Xml,
        Json,
    };

    /// @brief 프리팹 에셋입니다(루트 GameObject 상태 템플릿).
    class SW_API PrefabAsset
    {
    public:
        /** @brief 로드하지 않은 빈 프리팹으로 만듭니다. */
        PrefabAsset();

        /** @brief XML 에서 프리팹을 로드합니다(`<Prefab formatVersion="0" name="..."><GameObject …/></Prefab>` — 저장하는 모양 하나). */
        [[nodiscard]] bool loadFromXmlFile( string_view assetRelativePath );
        /** @brief JSON 에서 프리팹을 로드합니다(GameObject 상태 JSON 그대로 — `saveToJsonFile` 이 쓰는 모양 하나). */
        [[nodiscard]] bool loadFromJsonFile( string_view assetRelativePath );
        /** @brief 바이너리에서 프리팹을 로드합니다(쿠킹된 PFB2). */
        [[nodiscard]] bool loadFromBinaryFile( string_view assetRelativePath );
        /** @brief XML 로 저장합니다(<Prefab formatVersion="0" name="...">). */
        [[nodiscard]] bool saveToXmlFile( string_view assetRelativePath ) const;
        /** @brief JSON 으로 저장합니다(GameObject 상태 JSON 그대로). 상태를 JSON 으로 옮기지 못하면 XML 본문을 `xmlBody` 로 싸 둡니다. */
        [[nodiscard]] bool saveToJsonFile( string_view assetRelativePath ) const;
        /**
         * @brief 경로의 확장자로 형식을 골라 저장합니다(`.prefab.xml` · `.prefab.json`). 프리팹 경로가 아니면 쓰지 않고 false 입니다.
         * @details 에디터가 프리팹을 쓰는 길(Apply to Prefab · 격리 편집 저장)은 이것만 씁니다 — 로더 · 쿠커와 같은 규칙입니다.
         */
        [[nodiscard]] bool saveToFile( string_view assetRelativePath ) const;
        /** @brief Shipping 쿠킹용 PFB2 바이너리로 저장합니다(magic + version + name + 상태 데이터). */
        [[nodiscard]] bool saveToBinaryFile( string_view assetRelativePath ) const;
        /** @brief GameObject 상태에서 프리팹을 채웁니다. */
        void setFromGameObject( const GameObject* pGameObject );

        /** @brief 프리팹 이름을 반환합니다. */
        const string& getName() const { return _name; }
        /** @brief 직렬화된 본문 상태 데이터(XML 또는 JSON — `getStateFormat`)를 반환합니다. */
        const string& getStateData() const { return _stateData; }
        /** @brief 상태 본문의 형식입니다(읽을 때 정했습니다). */
        PrefabStateFormat getStateFormat() const { return _stateFormat; }
        /**
         * @brief 프리팹 상태를 오브젝트에 읽어 넣습니다. 오브젝트의 컴포넌트는 모두 다시 만들어집니다(오브젝트는 매니저에 속해야 합니다).
         * @details 스폰 · 되돌리기 · 오버라이드 비교(CDO) · 형식 변환이 모두 이것을 씁니다. 예전에는 다섯 자리가 본문 첫 글자('{')로 형식을 짐작했고,
         *          되돌리기 둘은 XML 로만 읽어 JSON 프리팹의 인스턴스를 비웠습니다(컴포넌트를 지운 뒤 읽기에 실패).
         *          다른 오브젝트로의 부착은 읽지 않습니다 — 프리팹 루트에는 부모가 없습니다(옛 프리팹에 남은 것도).
         * @param pIdentity 있으면 다시 만드는 컴포넌트가 그 id 를 되찾습니다(되돌리기 — 인스턴스의 컴포넌트를 가리키던 핸들이 이어진다).
         */
        [[nodiscard]] bool applyStateTo( GameObject* pTarget, const ObjectIdentity* pIdentity = nullptr ) const;
        /** @brief 로드에 성공했으면 true 입니다. */
        bool isValid() const { return _bValid == SW_TRUE; }
        /** @brief 상태 XML/JSON 안의 `.prefab` 경로를 수집합니다. */
        void collectReferencedPrefabPaths( vector<string>& outListPath ) const;

    private:
        /**
         * @brief 상태를 다른 형식의 텍스트로 옮깁니다(XML ↔ JSON). 매니저가 있는 임시 오브젝트를 거칩니다.
         * @details 예전에는 매니저 없는 `GameObject` 를 거쳐, 컴포넌트를 만들 팩토리가 없어 **컴포넌트를 모두 버린** 본문을 썼습니다.
         */
        string convertState( PrefabStateFormat targetFormat ) const;

        string            _name;
        string            _stateData;
        PrefabStateFormat _stateFormat;
        // `_bValid` 라는 이름은 이 저장소에서 **두 가지**다 — 여기와 `RenderFramePacket` 은
        // `uint8 : 1` 비트필드이고, `EditorWorkspace` 와 `SceneDocument` 는 진짜 `bool` 이다.
        // `Style/BitfieldBoolean` 린트는 이름으로만 판정하므로 그런 이름은 **일부러 건너뛴다**
        // (오탐보다 누락이 낫다는 판단). 그래서 여기서는 `SW_TRUE`/`SW_FALSE` 를 손으로 지킨다.
        uint8                  _bValid   : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };

    /// @brief 프리팹을 로드하고 스폰하는 캐시입니다.
    class SW_API PrefabManager final : public IAssetCache
    {
    public:
        /** @brief 빈 프리팹 캐시로 만듭니다. */
        PrefabManager() = default;
        /** @brief 캐시된 프리팹을 정리합니다. */
        ~PrefabManager() override = default;

        /** @brief 복사를 금지합니다. */
        PrefabManager( const PrefabManager& ) = delete;
        /** @brief 대입을 금지합니다. */
        PrefabManager& operator=( const PrefabManager& ) = delete;

        /** @brief 프리팹을 로드합니다. Dev 는 XML/JSON 저작본을, Shipping 은 쿠킹된 .prefab.bin 만 읽습니다. 캐시 키는 확장자를 뺀 정규화 경로입니다. */
        PrefabAsset* loadPrefab( string_view assetRelativePath );
        /**
         * @brief 인스턴스를 프리팹 상태로 되돌립니다. 인스턴스의 자리 — 부모 · 이름 · 루트의 위치와 회전 — 는 지킵니다.
         * @details 유니티 `PrefabUtility.RevertPrefabInstance` 와 같은 규칙입니다(루트의 위치 · 회전은 늘 인스턴스의 것, 스케일은 되돌린다). 예전에는
         *          상태를 통째로 읽어 넣어, 다른 오브젝트에 붙어 있던 인스턴스가 루트로 떨어지고 이름 · 자리가 프리팹의 것으로 바뀌었습니다.
         */
        [[nodiscard]] bool revertInstance( GameObject* pInstance, string_view assetRelativePath );
        /**
         * @brief 프리팹을 스폰합니다.
         * @details 컴포넌트 틱 중이면 오브젝트는 바로 돌려주고 프리팹 상태는 틱 직후(구조 변경 큐)에 채웁니다 — 그때까지 오브젝트는 비어 있습니다.
         *          상태를 쓰지 못하면 그 오브젝트를 지웁니다. 틱 안에서 스폰하고 바로 초기화하려면 `executeOrDeferPostTick` 으로 감쌀 것.
         *          씬에 놓인 인스턴스는 이것을 지나지 않습니다 — 씬은 프리팹 원형에 덮어쓴 것만 얹어 짓습니다(`Scene::instantiate` · `PrefabOverrides`).
         */
        GameObject* spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName = nullptr );
        /**
         * @brief 캐시에서 프리팹 하나를 버립니다. 다음 `loadPrefab` 이 디스크를 다시 읽습니다(에디터 핫 리로드).
         * @details **지금 살아 있는 오브젝트는 바뀌지 않습니다.** 보장하는 것은 "다음에 짓는 인스턴스에 고친 내용이 나온다" 입니다 — 스폰과,
         *          씬을 다시 읽을 때입니다. 씬은 프리팹 인스턴스의 덮어쓴 것만 저장하므로(`PrefabOverrides`) 다시 읽은 씬의 인스턴스에는
         *          고친 프리팹의 값이 퍼지고, 인스턴스가 덮어쓴 값은 그대로 남습니다.
         * @param pDevice 쓰지 않습니다. 프리팹은 GPU 자원을 들지 않습니다(`IAssetCache` 계약).
         */
        void reload( string_view assetRelativePath, IRHIDevice* pDevice = nullptr ) override;

        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return "Prefab"; }
        /** @brief 그 프리팹을 지금 캐시가 들고 있는지 반환합니다. */
        bool isCached( string_view assetRelativePath ) const override;
        /** @brief 지금 들고 있는 항목 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 캐시를 통째로 비웁니다. 다음 `loadPrefab` 이 디스크를 다시 읽습니다. */
        void clear() override;

        /**
         * @brief @p sourceRoot 아래(하위 폴더 포함)의 저작 프리팹(`*.prefab.xml` · `*.prefab.json`)을 `<cookedDir>/<상대 경로>/<이름>.prefab.bin` 으로 굽습니다.
         * @return 기록한 쿠킹본 수입니다. 읽지 못한 것과, 앞의 소스와 같은 쿠킹본을 쓰게 되는 소스(`x.prefab.xml` 과 `x.prefab.json`)는
         *         @p outFailedCount 로 세고 경고합니다 — Shipping 은 쿠킹본 하나만 읽으므로 어느 것이 이길지 정해 두면 안 됩니다.
         * @details 씬처럼 엔진이 굽습니다(`App --cook-scenes` 가 리소스 루트로 함께 부른다 — 언리얼 쿡 커맨드렛 자리). 예전에는 파이썬
         *          (`CookAssets.py`)이 PFB2 형식을 따로 들고 `.prefab.xml` 만 구워, `.prefab.json` 은 Shipping 에 쿠킹본이 없어 스폰이 실패했고
         *          엔진의 쿠킹 함수는 쓰이지 않았습니다. 형식을 쓰는 곳은 이제 `PrefabAsset::saveToBinaryFile` 하나입니다.
         */
        static uint32 cookAllPrefabs( string_view sourceRoot, string_view cookedDir, uint32& outFailedCount );

    private:
        /** @brief 스폰한 오브젝트에 프리팹 상태 · 이름을 씁니다. 바로 스폰과 틱 뒤로 미룬 스폰이 같이 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool applySpawnState( GameObject* pGameObject, const PrefabAsset& asset, string_view instanceName );

        mutable std::shared_mutex                      _mapCacheMutex;
        unordered_map<string, unique_ptr<PrefabAsset>> _mapCache;
    };
} // namespace sw
