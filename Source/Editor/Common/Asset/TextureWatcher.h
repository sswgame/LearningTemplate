#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Asset/TextureImportConfig.h"

namespace sw
{
    class IFileWatcher;
} // namespace sw

namespace sw::editor
{
    /**
     * @class TextureWatcher
     * @brief textures_raw 디렉터리를 실시간 감시하여 변경된 소스 이미지를 DDS로 자동 베이킹하는 백그라운드 워처
     */
    class TextureWatcher
    {
    public:
        TextureWatcher();
        ~TextureWatcher();

        TextureWatcher( const TextureWatcher& )            = delete;
        TextureWatcher& operator=( const TextureWatcher& ) = delete;

        /**
         * @brief 텍스처 감시자를 초기화하고 감시를 시작합니다.
         * @param rootDirectory 감시할 리소스 루트 디렉터리 (예: "Resource")
         * @param configPath 임포트 규칙 JSON 경로 (비어있으면 기본 "Config/Editor/TextureImportConfig.json" 로드)
         * @return 성공 여부
         */
        bool initialize( string_view rootDirectory, string_view configPath = {} );

        /**
         * @brief 감시를 중단하고 리소스를 해제합니다.
         */
        void shutdown();

        /**
         * @brief 에디터 매 프레임 업데이트에서 파일 변경 이벤트를 폴링하고 자동 베이킹을 수행합니다.
         */
        void update();

        /**
         * @brief 지정된 루트 디렉터리 내의 모든 textures_raw 텍스처를 일괄 검사하고 최신 상태로 베이킹합니다.
         * @param bForceAll true이면 타임스탬프와 무관하게 모든 텍스처를 강제 재베이킹
         * @return 베이킹 성공한 파일 수
         */
        uint32 bakeAll( bool bForceAll = false );

        /** @brief 현재 활성화 상태인지 확인합니다. */
        bool isActive() const { return _bActive == SW_TRUE; }

        /** @brief 로드된 텍스처 임포트 설정을 반환합니다. */
        const TextureImportConfig& getConfig() const { return _config; }

    private:
        bool processFileChange( string_view directory, string_view fileName );
        bool deriveOutputPath( string_view sourcePath, string& outOutputPath ) const;

    private:
        unique_ptr<IFileWatcher> _fileWatcher;
        TextureImportConfig      _config;
        string                   _watchDirectory;
        string                   _configPath;
        uint8                    _bActive  : 1;
        [[maybe_unused]] uint8   _reserved : 7;
    };
} // namespace sw::editor
