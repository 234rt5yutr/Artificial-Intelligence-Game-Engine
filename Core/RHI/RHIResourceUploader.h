#pragma once

#include "RHIBuffer.h"
#include "RHITexture.h"
#include <memory>

namespace Core {
namespace RHI {

    class RHIResourceUploader {
    public:
        virtual ~RHIResourceUploader() = default;

        // Begins a recording phase for uploads
        virtual void Begin() = 0;

        // Uploads data to a buffer
        virtual void UploadBufferData(std::shared_ptr<RHIBuffer> buffer, const void* data, std::size_t size, std::size_t offset = 0) = 0;

        // Uploads data to a texture
        virtual void UploadTextureData(std::shared_ptr<RHITexture> texture, const void* data, std::size_t size) = 0;

        // Ends the recording phase and submits the transfer commands to the queue.
        // Returns true if successfully submitted.
        virtual bool EndAndSubmit() = 0;
    };

} // namespace RHI
} // namespace Core