/**
 * @file MaterialParameterCollection.h
 * @brief 머티리얼 파라미터 묶음(`*.matparams.xml`)입니다. 파일 하나의 값을 머티리얼 여럿이 읽고, 런타임에 값을 바꾸면 그 묶음을 쓰는 모든 그림이 바뀝니다(언리얼 Material Parameter Collection).
 * @details **셰이더 슬롯을 새로 두지 않는다.** 머티리얼 프로퍼티가 묶음의 이름을 가리키고(`collection` · `collectionParameter`), 값이 바뀌면 그 머티리얼이 다음 프레임에 프로퍼티를 다시 패킹한다
 *          (`Material::syncParameterCollections`). 그래서 값은 리플렉션이 정한 자리의 머티리얼 데이터로 가고, 슬롯 · 바인딩 · 백엔드 코드가 그대로라 네 백엔드(dx11 · dx12 · vk · gl)에서 같은 길을 탄다.
 *          어떤 셰이더의 어떤 프로퍼티든 셰이더를 고치지 않고 묶을 수 있다.
 *          언리얼은 묶음마다 유니폼 버퍼 하나를 셰이더가 직접 읽어 값 바꾸기가 O(1) 이지만 셰이더 코드가 묶음의 배치를 알아야 하고 머티리얼마다 묶음 수에 제한이 있다.
 *          여기는 바꾼 값이 그 묶음을 쓰는 머티리얼 수만큼의 패킹이다. 머티리얼 원소는 프레임마다 어차피 사본을 비교하므로 값이 프레임마다 바뀌는 묶음을 머티리얼 수백 개가 쓰게 되면 그때 버퍼를 둔다.
 *
 *          게임 스레드 객체다. 같은 경로는 같은 객체이고 쥔 쪽(머티리얼)이 없어지면 사라져 다음에 파일 값으로 다시 시작한다.
 *          shortcut: 핫 리로드가 없다 — 파일을 고치면 다시 실행한다. 공유 캐시(`SharedAssetCache`)가 합쳐진 뒤 그 표에 올리면 된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
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
        static constexpr AssetTypeDef kAssetType{ "MaterialParameterCollection", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        PROPERTY( Tooltip = "Values shared by every material that binds this collection" )
        vector<MaterialCollectionParameterDef> _listParameter{};

        /** @brief 이름이 비거나 겹치면 오류를 남기고 false 입니다. */
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
        /** @brief 같은 경로의 묶음을 나눠 받습니다. 처음이면 파일을 읽습니다. 읽지 못하면 오류를 남기고 nullptr 입니다. */
        [[nodiscard]] static shared_ptr<MaterialParameterCollection> acquire( const AssetID& assetID );

        /** @brief @p desc 의 값으로 만듭니다(`acquire` · 테스트). */
        explicit MaterialParameterCollection( MaterialParameterCollectionDef desc );

        /** @brief 지금 값입니다. 없는 이름이면 nullptr 입니다. */
        [[nodiscard]] const float4* findValue( const hashed_string& name ) const;
        /** @brief 값을 바꿉니다. 묶은 모든 머티리얼이 다음 프레임에 따릅니다. 없는 이름이면 오류를 남기고 false 입니다. 같은 값이면 세대가 그대로입니다. */
        bool setValue( const hashed_string& name, const float4& value );
        /** @brief 값이 바뀔 때마다 오르는 세대입니다. */
        [[nodiscard]] uint32 getGeneration() const { return _generation; }

        /** @brief 값의 앞 @p componentCount 채널을 머티리얼 프로퍼티 값 글(`0.5 0.25`)로 씁니다. */
        [[nodiscard]] static string makePropertyText( const float4& value, uint32 componentCount );

    private:
        struct Entry
        {
            hashed_string _name;
            float4        _value;
        };

        vector<Entry> _listEntry;
        uint32        _generation;
    };
} // namespace sw
