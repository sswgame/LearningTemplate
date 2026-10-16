/**
 * @file Render2DSettings.h
 * @brief 프로젝트의 2D 렌더 설정(`render2d.xml`)입니다. 정렬 레이어 목록과 투명 정렬 축을 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @enum TransparencySortMode
     * @brief 같은 정렬 레이어와 순서 안에서 투명 물체의 앞뒤를 무엇으로 가를지 정합니다(유니티 `TransparencySortMode`).
     */
    enum class TransparencySortMode : uint8
    {
        Auto = 0,   ///< 원근 카메라는 눈까지의 거리, 직교 카메라는 시선 축 깊이를 씁니다(유니티 Default)
        Distance,   ///< 항상 눈까지의 거리를 씁니다(유니티 Perspective)
        ViewAxis,   ///< 항상 시선 축 깊이를 씁니다(유니티 Orthographic)
        CustomAxis, ///< 데이터가 지정한 축 위의 깊이를 씁니다. 탑다운이면 (0, 1, 0) 으로 위쪽 것이 뒤에 그려집니다(유니티 Custom Axis)
    };

    /**
     * @class Render2DSettings
     * @brief 2D 렌더러가 프로젝트 단위로 읽는 설정입니다. 정렬 레이어 이름 목록(앞 항목이 먼저 그려집니다)과 투명 정렬 축을 담습니다.
     * @details 유니티의 Sorting Layers(Tags & Layers)와 Transparency Sort Mode(Graphics 설정), Godot 의 CanvasLayer 순서와 z_index 에 해당합니다.
     *
     *          **정렬 키**는 32비트 하나입니다: `(레이어 순번 << 16) | (레이어 안 순서 + 0x8000)`. 투명 큐는 이 키가 작은 것부터 그립니다.
     *          키가 같으면 깊이(먼 것 먼저), 깊이도 같으면 후보 번호(등록 순서)로 가립니다. 전순서이므로 같은 Z 의 두 스프라이트가
     *          카메라를 따라 순서가 뒤집히지 않습니다. 키 0 은 "기본(Default 레이어, 순서 0)"을 뜻하는 플레이스홀더입니다.
     *          레이어 안 순서를 ±32767 로 제한하므로 유효한 키는 0 이 될 수 없습니다. 메시 컴포넌트는 설정을 읽지 않고 0 을 보관하며,
     *          빌더가 이를 설정의 기본 키로 바꿉니다.
     *
     *          파일은 엔진 기본(`engine/data/render2d.xml`)입니다. 활성 게임 팩에 `data/render2d.xml` 이 있으면 그 파일이 **통째로** 대신합니다.
     *          모르는 요소, 속성, 열거자와 같은 이름의 레이어 둘, `Default` 레이어가 없는 설정은 읽기 오류입니다.
     *
     *          스레드: 활성 설정(`getActive`)은 처음 부를 때 한 번 읽고, 그 뒤로는 읽기만 합니다. `reloadActive` 는 게임 스레드에서만 부릅니다.
     */
    class SW_API Render2DSettings
    {
    public:
        /** @brief 레이어 안 순서의 범위입니다(유니티 Order in Layer 와 같은 16비트). 범위 밖의 값은 범위 안으로 제한합니다. */
        static constexpr int32 kMinOrderInLayer = -32767;
        static constexpr int32 kMaxOrderInLayer = 32767;
        /** @brief "기본 레이어, 순서 0"을 뜻하는 플레이스홀더 키입니다. 유효한 키는 0 이 될 수 없습니다. */
        static constexpr uint32 kDefaultSortKeyPlaceholder = 0;
        /** @brief 정렬 레이어 수 상한입니다(키의 상위 16 비트). */
        static constexpr uint32 kMaxSortingLayerCount = 0xFFFF;

        /** @brief `Default` 레이어 하나와 정렬 Auto 를 가진 설정을 만듭니다(파일을 읽지 못했을 때의 값입니다). */
        Render2DSettings();

        /** @brief 리소스 경로의 XML 을 읽습니다. 실패하면 오류를 남기고 false 를 반환하며 설정은 바뀌지 않습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief XML 본문을 읽습니다. 실패하면 오류를 남기고 false 를 반환하며 설정은 바뀌지 않습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );

        /** @brief 정렬 레이어 수입니다. */
        uint32 getSortingLayerCount() const { return static_cast<uint32>( _listSortingLayer.size() ); }
        /** @brief 순번에 해당하는 레이어 이름입니다. 범위 밖이면 빈 이름입니다. */
        const hashed_string& getSortingLayerName( uint32 layerIndex ) const;
        /** @brief 이름에 해당하는 레이어 순번입니다(대소문자 무시). 없으면 -1 입니다. */
        int32 findSortingLayer( const hashed_string& layerName ) const;
        /** @brief `Default` 레이어의 순번입니다. */
        uint32 getDefaultSortingLayer() const { return _defaultLayerIndex; }

        /** @brief 레이어 순번과 레이어 안 순서로 정렬 키를 만듭니다. 순서는 [kMinOrderInLayer, kMaxOrderInLayer] 로 제한합니다. */
        static uint32 makeSortKey( uint32 layerIndex, int32 orderInLayer );
        /** @brief `Default` 레이어, 순서 0 의 키입니다. 빌더가 플레이스홀더 키(0)를 이 값으로 바꿉니다. */
        uint32 getDefaultSortKey() const { return makeSortKey( _defaultLayerIndex, 0 ); }
        /**
         * @brief 이름과 순서로 키를 만듭니다. 이름을 모르면 false 를 반환하고 @p outSortKey 는 기본 키가 됩니다. 호출한 쪽이 잘못된 이름을 알려야 합니다.
         * @details 빈 이름은 `Default` 입니다.
         */
        bool resolveSortKey( const hashed_string& layerName, int32 orderInLayer, uint32& outSortKey ) const;

        /** @brief 투명 정렬 방식입니다. */
        TransparencySortMode getTransparencySortMode() const { return _sortMode; }
        /** @brief CustomAxis 의 축입니다(정규화됨). */
        const float3& getTransparencySortAxis() const { return _sortAxis; }
        /**
         * @brief 이 카메라에서 투명 정렬에 쓸 축입니다. 영벡터면 "눈까지의 거리"를 쓰고, 아니면 그 축 위의 깊이(카메라에서 그 축으로 측정한 값)를 씁니다.
         * @param bOrthographic 카메라가 직교 투영인가
         * @param cameraForward 카메라가 보는 방향(정규화)
         */
        float3 computeTransparentSortAxis( bool bOrthographic, const float3& cameraForward ) const;

        /**
         * @brief 활성 설정입니다. 처음 부르면 활성 게임 팩의 `data/render2d.xml` 을 읽고, 없으면 `engine/data/render2d.xml` 을 읽습니다.
         * @details 둘 다 읽지 못하면 기본 설정(`Default` 하나)을 쓰고 오류를 한 번 남깁니다.
         */
        static const Render2DSettings& getActive();
        /** @brief 활성 설정을 파일에서 다시 읽습니다(설정 핫 리로드, 게임 전환). 게임 스레드에서만 부릅니다. */
        static void reloadActive();
        /** @brief 엔진 기본 파일 경로입니다. */
        static string_view getEngineSettingsPath() { return "engine/data/render2d.xml"; }

    private:
        vector<hashed_string> _listSortingLayer;  ///< 앞 항목이 먼저(뒤에) 그려집니다
        float3                _sortAxis;          ///< CustomAxis 의 축(정규화)
        uint32                _defaultLayerIndex; ///< `Default` 의 순번
        TransparencySortMode  _sortMode;
    };
} // namespace sw
