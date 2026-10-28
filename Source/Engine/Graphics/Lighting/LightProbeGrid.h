/**
 * @file LightProbeGrid.h
 * @brief 라이트 프로브 격자 하나의 베이크 결과(`.probes`)입니다. 균일 격자의 프로브마다 2차 구면 조화 함수(SH9 RGB)를 담습니다.
 * @details 유니티 APV 처럼 축 정렬 균일 격자입니다. 격자는 시작점(가장 낮은 모서리의 프로브), 축마다 간격, 축마다 프로브 수로 정해지고, 프로브 번호는
 *          (z × Y + y) × X + x 입니다. 베이크(CPU, P2)가 쓰고 볼륨 컴포넌트(`LightProbeVolumeComponent`)가 읽습니다. 결과는 커밋합니다 — CI 에 GPU 가 없어
 *          내비메시처럼 결과를 저장소에 둡니다.
 *
 *          파일(리틀 엔디언): 매직 `SWLP`, 버전, 시작점 float3, 간격 float3, 프로브 수 uint32 셋, 프로브마다 float32 27 개(`SH9Color::_arrCoefficient` 순서).
 *          볼륨의 격자(위치 · 크기 · 간격)와 파일의 격자가 다르면 그 파일은 다른 볼륨의 베이크다 — 읽는 쪽이 오류로 멈추고 쓰지 않는다(다시 베이크한다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/SphericalHarmonics.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Common/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct LightProbeGridLayout
     * @brief 격자의 모양입니다 — 시작점, 축마다 간격, 축마다 프로브 수. 볼륨이 자기 트랜스폼에서 만들고 파일이 같은 값을 싣습니다.
     */
    struct SW_API LightProbeGridLayout
    {
        float3 _origin{};  ///< 가장 낮은 모서리 프로브의 월드 위치
        float3 _spacing{}; ///< 축마다 프로브 사이 거리(m), 모두 0 보다 크다
        uint32 _countX{ 0 };
        uint32 _countY{ 0 };
        uint32 _countZ{ 0 };

        /** @brief 프로브 수입니다. */
        uint32 getProbeCount() const { return _countX * _countY * _countZ; }
        /** @brief 격자 칸 (x, y, z) 의 프로브 번호입니다. */
        uint32 computeProbeIndex( uint32 cellX, uint32 cellY, uint32 cellZ ) const { return ( cellZ * _countY + cellY ) * _countX + cellX; }
        /** @brief 프로브 번호의 월드 위치입니다. */
        float3 computeProbePosition( uint32 probeIndex ) const;
        /** @brief 모양이 같은지 봅니다(위치 · 간격은 @p tolerance 안에서). 다르면 다른 볼륨의 베이크입니다. */
        bool matches( const LightProbeGridLayout& other, float32 tolerance ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief `.probes` 하나의 내용입니다. 파일 머리말을 참고하세요. */
    struct SW_API LightProbeGrid
    {
        static constexpr uint32      kMagic     = FourCcUtil::make( "SWLP" );
        static constexpr uint32      kVersion   = 1;
        static constexpr string_view kExtension = ".probes";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `LightProbeGrid.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "LightProbeGrid", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        LightProbeGridLayout _layout{};
        vector<SH9Color>     _listProbe; ///< 프로브마다 하나, 번호 순서(`LightProbeGridLayout::computeProbeIndex`)

        /** @brief 모양과 프로브 수가 맞는지(간격이 양수, 프로브가 모양만큼 있는지) 봅니다. */
        [[nodiscard]] bool isValid( string* pOutError = nullptr ) const;
        /** @brief 비웁니다. */
        void clear();

        /** @brief 바이트로 만듭니다(리틀 엔디언). */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직, 버전, 길이, 구조가 틀리면 false 이고 내용을 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t size, string* pOutError = nullptr );
        /** @brief 파일로 씁니다. 부모 폴더가 없으면 만듭니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief 리소스 경로(또는 절대 경로)에서 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, string* pOutError = nullptr );
    };
} // namespace sw
