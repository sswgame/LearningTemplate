/**
 * @file ParkLayoutPreview.h
 * @brief ThemePark 배치 파일을 읽어 에디터가 겹쳐 그릴 도형을 만듭니다(ImGui 에 의존하지 않습니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

namespace sw::editor
{
    /** @brief 놀이기구 하나의 미리보기입니다. */
    struct ParkRidePreview
    {
        string  _name;
        float3  _position{};
        float3  _size{};
        float4  _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32   _buildCost{ 0 };
        int32   _capacity{ 0 };
        float32 _loadTime{ 0.0f };
        bool    _bCoaster{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ParkLayoutPreview
     * @brief 배치 파일 하나(기본 `game/themepark/data/rides.xml`)를 키트의 로더로 읽어 보관합니다. 파일 내용이 바뀌면 다시 읽습니다.
     * @details 이 확장이 기여하는 에디터 서브시스템이다(`SW_EDITOR_SUBSYSTEM`) — 패널과 시각화가 `editor::getService<ParkLayoutPreview>()` 로 같이 본다.
     */
    class ParkLayoutPreview final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ParkLayoutPreview";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 놀이기구 배치 파일과 코스터 레이아웃 파일의 경로입니다. 게임(`ParkDirectorComponent`)과 같은 경로를 씁니다. */
        static constexpr const utf8* kLayoutPath  = "game/themepark/data/rides.xml";
        static constexpr const utf8* kCoasterPath = "game/themepark/data/coasters.xml";

        ParkLayoutPreview();

        /**
         * @brief 파일 내용이 바뀌었으면 다시 읽습니다. 다시 읽었으면(처음, 바뀜, @p bForce) true 를 반환합니다.
         * @details 파일이 없거나 틀리면 미리보기를 비우고 한 번 경고한다.
         */
        bool refresh( bool bForce = false );
        /** @brief 놀이기구 영역(바닥 사각형과 기둥), 입구 십자, 코스터 트랙의 월드 선분을 @p outListSegment 뒤에 붙입니다. */
        void appendSegments( vector<EditorWorldSegment>& outListSegment ) const;

        const vector<ParkRidePreview>& getRides() const { return _listRide; }
        const float3&                  getGatePosition() const { return _gatePosition; }
        bool                           isLoaded() const { return _bLoaded; }

    private:
        vector<ParkRidePreview> _listRide;
        vector<vector<float3>>  _listTrackPoint; ///< 코스터마다의 트랙 점이다(키트의 `CoasterTrackBuilder` 결과)
        float3                  _gatePosition;
        uint64                  _contentHash;
        bool                    _bLoaded;
        bool                    _bWarned;
    };
} // namespace sw::editor
