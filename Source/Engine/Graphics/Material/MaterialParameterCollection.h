/**
 * @file MaterialParameterCollection.h
 * @brief 머티리얼 파라미터 묶음(`*.matparams.xml`)입니다. 파일 하나의 값을 머티리얼 여럿이 읽고, 런타임에 값을 바꾸면 그 묶음을 쓰는 모든 그림이 바뀝니다(언리얼 Material Parameter Collection).
 * @details **셰이더 슬롯을 새로 두지 않는다.** 머티리얼 프로퍼티가 묶음의 이름을 가리키고(`collection` · `collectionParameter`), 값이 바뀌면 그 머티리얼이 다음 프레임에 그 칸만 다시 쓴다
 *          (`Material::syncParameterCollections`). 그래서 값은 리플렉션이 정한 자리의 머티리얼 데이터로 가고, 슬롯 · 바인딩 · 백엔드 코드가 그대로라 네 백엔드(dx11 · dx12 · vk · gl)에서 같은 길을 탄다.
 *          어떤 셰이더의 어떤 프로퍼티든 셰이더를 고치지 않고 묶을 수 있다.
 *          여기는 바꾼 값이 그 묶음을 쓰는 머티리얼 수만큼의 패킹이다. 많은 머티리얼이 읽고 자주 바뀌는 값(날씨)은 값에 `_global` 을 적는다 — 셰이더가 프레임 상수(`MaterialGlobals`)에서
 *          바로 읽어 값 바꾸기가 O(1) 이다(언리얼 묶음의 유니폼 버퍼 자리). 전역 값에는 프로퍼티를 묶지 않는다(오류). 전역을 먹이는 묶음은 엔진 기본 에셋(`EngineDefaultAssets::_materialGlobals`) 하나다.
 *
 *          게임 스레드 객체다. 같은 경로는 같은 객체이고 쥔 쪽(머티리얼)이 없어지면 사라져 다음에 파일 값으로 다시 시작한다.
 *          표는 에셋 매니저의 내장 캐시(`MaterialParameterCollectionCache`)다 — 파일을 고치면 핫 리로드가 쥔 묶음을 제자리에서 파일 값으로 바꾸고(`replaceValues`)
 *          세대를 올려, 묶은 머티리얼이 다음 프레임에 다시 패킹한다. 공유 캐시(`SharedAssetCache`)는 `const` 에셋을 나눠 주는데 묶음은 런타임에 값을 바꾸므로 쓰지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Asset/Cache/WeakInternTable.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Graphics/Material/MaterialGlobals.h"
#include "Engine/Graphics/Material/MaterialRenderVersion.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 묶음의 값 하나입니다. */
    REFLECT()
    struct SW_API MaterialCollectionParameterDef
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Meta = "NotLocalizable", Tooltip = "Parameter name materials bind to" )
        string _name{};
        PROPERTY( Tooltip = "Value; a material property reads as many channels as its type has" )
        float4 _value{};
        PROPERTY( Tooltip = "Shader global this value feeds (PassCB); None binds through material properties" )
        MaterialGlobalSlot _global{ MaterialGlobalSlot::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `*.matparams.xml` 하나입니다. 변형은 루트의 `_base` 로 적고 값을 이름으로 합칩니다(계절 · 지역별 묶음).
     * @code
     *     <MaterialParameterCollectionDef>
     *       <_listParameter><MaterialCollectionParameterDef _name="WorldTint" _value="1, 0.9, 0.8, 1" /></_listParameter>
     *     </MaterialParameterCollectionDef>
     * @endcode
     */
    REFLECT()
    struct SW_API MaterialParameterCollectionDef
    {
        REFLECT_BODY();

        /** @brief 묶음 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".matparams.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `MaterialParameterCollection.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "MaterialParameterCollection", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        PROPERTY( Tooltip = "Values shared by every material that binds this collection" )
        vector<MaterialCollectionParameterDef> _listParameter{};

        /** @brief 이름이 비거나 겹치거나, 한 전역 칸을 둘이 먹이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate( string_view sourceLabel ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class MaterialParameterCollection
     * @brief 묶음 하나의 지금 값입니다. 값이 바뀌면 세대가 올라 묶은 머티리얼이 다음 동기화에 다시 패킹합니다.
     */
    class SW_API MaterialParameterCollection
    {
    public:
        /** @brief 같은 경로의 묶음을 나눠 받습니다(에셋 매니저의 `MaterialParameterCollectionCache`). 처음이면 파일을 읽습니다. 읽지 못하면 오류를 남기고 nullptr 입니다. */
        [[nodiscard]] static shared_ptr<MaterialParameterCollection> acquire( const AssetID& assetID );

        /** @brief @p desc 의 값으로 만듭니다(`acquire` · 테스트). */
        explicit MaterialParameterCollection( MaterialParameterCollectionDef desc );

        /** @brief 지금 값입니다. 없는 이름이면 nullptr 입니다. */
        [[nodiscard]] const float4* findValue( const hashed_string& name ) const;
        /** @brief 값을 바꿉니다. 묶은 모든 머티리얼이 다음 프레임에 따릅니다. 없는 이름이면 오류를 남기고 false 입니다. 같은 값 · 전역 값(묶은 머티리얼이 없다)이면 세대가 그대로입니다. */
        bool setValue( const hashed_string& name, const float4& value );
        /**
         * @brief 값 전체를 @p desc(다시 읽은 파일)로 바꾸고 세대를 올립니다. 런타임에 바꾼 값은 파일 값으로 돌아갑니다(파일이 정본이다 — 언리얼에서 묶음 에셋의 기본값을 고친 것과 같다).
         * @details 같은 객체를 고치므로 쥔 머티리얼이 새 객체를 찾지 않는다. 없어진 이름을 묶은 머티리얼은 다음 동기화에서 오류를 남긴다.
         */
        void replaceValues( MaterialParameterCollectionDef desc );
        /** @brief 묶은 머티리얼이 읽는 값이 바뀔 때마다 오르는 세대입니다. */
        [[nodiscard]] uint32 getGeneration() const { return _generation.get(); }
        /** @brief @p name 이 셰이더 전역 값이면 true 입니다 — 머티리얼 프로퍼티는 묶지 못한다. */
        [[nodiscard]] bool isGlobal( const hashed_string& name ) const;
        /** @brief 전역 칸을 가진 값들을 @p inoutGlobals 에 씁니다. 모든 전역 칸(`MaterialGlobalSlot`)을 먹였으면 true 입니다(빠진 칸은 그대로다). */
        [[nodiscard]] bool writeGlobals( MaterialGlobals& inoutGlobals ) const;

    private:
        struct Entry
        {
            hashed_string      _name;
            float4             _value;
            MaterialGlobalSlot _global{ MaterialGlobalSlot::None };
        };

        vector<Entry>         _listEntry;
        MaterialRenderVersion _generation; ///< 값이 바뀔 때마다 오른다 — 묶은 머티리얼이 다시 패킹하므로 렌더 값의 전역 번호도 오른다
    };
} // namespace sw

namespace sw
{
    /**
     * @class MaterialParameterCollectionCache
     * @brief 쥔 쪽이 있는 묶음의 표입니다. 에셋 매니저가 내장 캐시로 소유합니다(`SW_BUILT_IN_ASSET_CACHE`).
     * @details 예전에는 함수 정적 표라 레지스트리에 보이지 않아 핫 리로드 · 합성 문맥 다시 읽기 · 재초기화 비우기가 닿지 않았다. 이제 다른 경로 캐시와 같은 길을 탄다:
     *          핫 리로드(개발 전용)와 합성 문맥 변경은 쥔 묶음을 제자리에서 파일 값으로 바꾸고, `clear` 는 표만 비운다(쥔 쪽의 묶음은 산다).
     *          공유 에셋 표(`SharedAssetTable` — 새 판 넣기)와 달리 제자리인 것은 묶음이 에셋이 아니라 **런타임에 바뀌는 값**이기 때문이다 — 게임이 `setValue` 로 늘 고치고,
     *          다시 읽기도 같은 종류의 쓰기다(언리얼 `UMaterialParameterCollectionInstance` 도 월드마다 하나인 고치는 객체다). 읽고 쓰는 쪽은 게임 스레드뿐이다
     *          (`setValue` · `replaceValues` · 머티리얼 동기화 `Material::syncParameterCollections`) — 워커 · 렌더 스레드는 세대가 오른 뒤 패킹한 값만 본다.
     */
    class SW_API MaterialParameterCollectionCache final : public AssetCacheBase
    {
    public:
        /** @brief 레지스트리의 종류 이름입니다(`AssetManager::getAssetCache<T>()`). 에셋 종류 이름과 같다. */
        static constexpr const utf8* kAssetKindName = "MaterialParameterCollection";

        MaterialParameterCollectionCache();

        /** @brief 같은 경로의 묶음을 나눠 받습니다. 처음이면 파일을 읽고, 읽지 못하면 오류를 남기고 nullptr 입니다. */
        [[nodiscard]] shared_ptr<MaterialParameterCollection> acquire( const AssetID& assetID );
        /** @brief 지금 쥔 묶음입니다. 없으면 nullptr 이고 파일을 읽지 않습니다(감시 값 · 진단). */
        [[nodiscard]] shared_ptr<MaterialParameterCollection> findLive( const AssetID& assetID ) const;
        /** @brief 쥔 묶음이면 파일을 다시 읽어 제자리에서 값을 바꿉니다(핫 리로드). 읽지 못하면 오류를 남기고 지금 값을 그대로 씁니다. */
        [[nodiscard]] ReloadResult reload( const AssetID& assetID );

        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /** @brief 그 경로의 묶음을 누가 쥐고 있으면 true 입니다. */
        bool   isCached( string_view relativePath ) const override;
        size_t getCachedCount() const override { return _table.countLive(); }
        /** @brief 쥔 묶음마다 경로 · 객체 크기 · 쥔 쪽 수 한 줄입니다. */
        void collectMemoryEntries( vector<AssetMemoryEntry>& outListEntry ) const override
        {
            _table.forEachLive( [&outListEntry]( const string& key, const MaterialParameterCollection& /*collection*/, uint32 referenceCount )
            { outListEntry.push_back( AssetMemoryEntry{ key, sizeof( MaterialParameterCollection ), 0, referenceCount } ); } );
        }
        /** @brief 표를 비웁니다. 쥔 쪽의 묶음은 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;
        /** @brief 쥔 묶음이면 합성 문맥이 바뀐 파일을 다시 읽어 제자리에서 바꿉니다. 바꿨으면 true 입니다. */
        bool reloadComposed( string_view relativePath ) override;

    private:
        /** @brief 파일을 읽어 검사합니다. 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool readDesc( const AssetID& assetID, MaterialParameterCollectionDef& outDesc );
        /** @brief 쥔 묶음을 다시 읽습니다(`reload` · `reloadComposed` 의 본문). 쥔 쪽이 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] bool reloadLive( const AssetID& assetID );

    private:
        WeakInternTable<string, MaterialParameterCollection> _table;
    };
} // namespace sw
