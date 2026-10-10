/**
 * @file SurfaceState.h
 * @brief 몸 표면 상태 — 몸 영역마다 머티리얼 값 채널(젖음 · 흙 · 피 · 상처 …)과, 부품마다 UV 공간 마스크(맞은 자리 도장 · 찢김 구멍)입니다.
 * @details 채널은 데이터 표(`SurfaceChannelTable`, `*.surfacechannels.xml`) 한 줄씩입니다 — 이름 · 초당 감쇠 · 최댓값 · UV 마스크 해상도 ·
 *          알파 잘라 내기(찢김). 영역 값은 머티리얼 파라미터로(영역마다 채널 순서의 벡터), UV 마스크는 텍스처로 올라갑니다(통합 쪽 일).
 *          찢김 채널은 머티리얼이 알파로 잘라 구멍을 내고, 다 찢긴 삼각형은 잘라 내기와 같은 보임 마스크 길(`FitPartResult::hideTriangles`)로
 *          조립된 메시에서 뺍니다(`SurfaceMaskUtil::markTornTriangles`).
 *
 *          XML 예:
 *          @code
 *          <SurfaceChannels>
 *            <Channel name="Wet" decayPerSecond="0.05"/>
 *            <Channel name="Blood" decayPerSecond="0.01" maskResolution="64"/>
 *            <Channel name="Tear" maskResolution="128" clip="true"/>
 *          </SurfaceChannels>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AppearanceGeometry;

    class CharacterDataReader;
    class XMLNode;

    /** @brief 표면 채널 하나입니다. 마스크 해상도가 0 이면 영역 값만 있고 UV 마스크가 없습니다. */
    struct SW_API SurfaceChannelDef
    {
        hashed_string _name{};
        float32       _decayPerSecond{ 0.0f };
        float32       _maxValue{ 1.0f };
        uint16        _maskResolution{ 0 };
        uint8         _bClip{ SW_FALSE }; ///< 머티리얼이 알파로 잘라 구멍을 낸다(찢김)
    };
} // namespace sw

namespace sw
{
    /** @brief 표면 채널 표입니다. */
    class SW_API SurfaceChannelTable
    {
    public:
        /** @brief XML 텍스트에서 읽습니다(비우고). 모르는 속성 · 원소 · 겹친 이름은 오류입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 채널을 더합니다(같은 이름은 바꿈). */
        void addChannel( const SurfaceChannelDef& channel );
        /** @brief 이름의 채널 번호입니다. 없으면 -1 입니다. */
        int32 findChannelIndex( const hashed_string& name ) const;
        /** @brief 채널들입니다. */
        const vector<SurfaceChannelDef>& getChannels() const { return _listChannel; }

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );

    private:
        vector<SurfaceChannelDef> _listChannel;
    };
} // namespace sw

namespace sw
{
    /** @brief UV 공간 정사각 마스크(값 0..최댓값)입니다. 도장 · 표본 · 감쇠를 합니다. */
    class SW_API SurfaceMask
    {
    public:
        SurfaceMask();

        /** @brief 해상도 × 해상도 칸을 0 으로 둡니다. */
        void initialize( uint16 resolution );
        /**
         * @brief UV 원(@p uvCenter, 반지름 @p uvRadius — UV 단위) 안의 칸을 @p value 이상으로 올립니다(큰 쪽을 남김). 가장자리 한 칸 폭은 부드럽게.
         * @return 바뀐 칸 수입니다.
         */
        uint32 stampCircle( const float2& uvCenter, float32 uvRadius, float32 value );
        /** @brief UV 자리의 값입니다(가장 가까운 칸, UV 는 [0, 1] 로 묶음). */
        float32 sample( const float2& uv ) const;
        /** @brief 모든 칸에서 @p amount 를 뺍니다(0 아래는 0). */
        void decay( float32 amount );
        /** @brief 해상도입니다. 0 이면 마스크가 없습니다. */
        uint16 getResolution() const { return _resolution; }
        /** @brief 칸 값들(행 우선, v 가 행)입니다 — 텍스처로 올릴 때 읽습니다. */
        const vector<float32>& getTexels() const { return _listTexel; }

    private:
        vector<float32> _listTexel;
        uint16          _resolution;
    };
} // namespace sw

namespace sw
{
    /** @brief 캐릭터 하나의 표면 상태 — 영역 × 채널 값과 부품 × 마스크 채널의 UV 마스크입니다. */
    class SW_API CharacterSurfaceState
    {
    public:
        /** @brief 채널 표 · 영역 수 · 부품 수로 상태를 만듭니다(모두 0). 표는 복사해 둡니다. */
        void initialize( const SurfaceChannelTable& channels, uint32 regionCount, uint32 partCount );

        /** @brief 영역의 채널 값을 정합니다(0..최댓값으로 묶음). 모르는 채널 · 영역이면 false 입니다. */
        bool setRegionValue( uint32 region, const hashed_string& channel, float32 value );
        /** @brief 영역의 채널 값에 더합니다. 모르는 채널 · 영역이면 false 입니다. */
        bool addRegionValue( uint32 region, const hashed_string& channel, float32 amount );
        /** @brief 영역의 채널 값입니다. 모르면 0 입니다. */
        float32 getRegionValue( uint32 region, const hashed_string& channel ) const;
        /** @brief 영역의 머티리얼 파라미터(채널 순서의 값들)를 채웁니다(비우고 채움). */
        void getRegionParameters( uint32 region, vector<float32>& outListValue ) const;

        /**
         * @brief 맞은 자리 도장 — 부품의 그 채널 UV 마스크에 원을 찍고, 영역 값도 @p value 만큼 올립니다(영역이 `kNoGroup` 이면 영역은 건너뜀).
         * @return 모르는 채널 · 부품이면 false 입니다. 마스크가 없는 채널은 영역 값만 올립니다.
         */
        bool stampHit( uint32 part, uint16 region, const hashed_string& channel, const float2& uvCenter, float32 uvRadius, float32 value );
        /** @brief 부품의 채널 마스크입니다. 없으면 nullptr 입니다. */
        const SurfaceMask* findMask( uint32 part, const hashed_string& channel ) const;

        /** @brief 시간이 흐릅니다 — 채널마다 초당 감쇠만큼 영역 값 · 마스크를 내립니다(감쇠 0 인 채널 — 찢김 — 은 그대로). */
        void tick( float32 deltaSeconds );

    private:
        vector<SurfaceChannelDef> _listChannel;
        vector<float32>           _listRegionValue; /**< 영역 × 채널(행 = 영역) */
        vector<SurfaceMask>       _listMask;        /**< 부품 × 채널(행 = 부품), 마스크 없는 채널은 해상도 0 */
        uint32                    _regionCount{ 0 };
        uint32                    _partCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 마스크 → 형상 도우미입니다(전부 static). */
    struct SW_API SurfaceMaskUtil
    {
        /**
         * @brief 다 찢긴 삼각형(세 정점 UV 와 무게중심 UV 의 마스크 값이 모두 @p threshold 이상)을 표시합니다(비우고 채움).
         * @return 표시한 수입니다. 형상에 UV 가 없으면 0 입니다.
         */
        static uint32 markTornTriangles( const AppearanceGeometry& geometry, const SurfaceMask& mask, float32 threshold, vector<uint8>& outListTorn );
    };
} // namespace sw
