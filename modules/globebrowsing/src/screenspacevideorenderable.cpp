/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/globebrowsing/src/screenspacevideorenderable.h>

#include <ghoul/filesystem/filesystem.h>

namespace {
    constexpr std::string_view _loggerCat = "ScreenSpaceVideoRenderable";

    constexpr openspace::properties::Property::PropertyInfo FileInfo = {
        "File",
        "File",
        "The file path that is used for this video provider. The file must point to a "
        "video that is then loaded and used for all tiles"
    };

    struct [[codegen::Dictionary(FfmpegTileProvider)]] Parameters {
        // [[codegen::verbatim(FileInfo.description)]]
        std::filesystem::path file;
    };

#include "screenspacevideorenderable_codegen.cpp"

} // namespace

namespace openspace {

documentation::Documentation ScreenSpaceVideoRenderable::Documentation() {
    return codegen::doc<Parameters>("skybrowser_screenspacevideorenderable");
}

ScreenSpaceVideoRenderable::ScreenSpaceVideoRenderable(const ghoul::Dictionary& dictionary)
    : ScreenSpaceRenderable(dictionary)
{
    _identifier = makeUniqueIdentifier(_identifier);

    // Handle target dimension property
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _videoFile = p.file;
}

ScreenSpaceVideoRenderable::~ScreenSpaceVideoRenderable() {

}

bool ScreenSpaceVideoRenderable::initialize() {
    return true;
}

bool ScreenSpaceVideoRenderable::deinitialize() {
    return true;
}

bool ScreenSpaceVideoRenderable::initializeGL() {
    ScreenSpaceRenderable::initializeGL();

    std::string path = absPath(_videoFile).string();

    // Open video
    int openRes = avformat_open_input(
        &_formatContext,
        path.c_str(),
        nullptr,
        nullptr
    );
    if (openRes < 0) {
        throw ghoul::RuntimeError(fmt::format("Failed to open input for file {}", path));
    }

    // Find stream info
    if (avformat_find_stream_info(_formatContext, nullptr) < 0) {
        throw ghoul::RuntimeError(fmt::format("Failed to get stream info for {}", path));
    }
    // DEBUG dump info
    av_dump_format(_formatContext, 0, path.c_str(), false);

    // Find the video stream
    for (unsigned int i = 0; i < _formatContext->nb_streams; ++i) {
        AVMediaType codec = _formatContext->streams[i]->codecpar->codec_type;
        if (codec == AVMEDIA_TYPE_VIDEO) {
            _streamIndex = i;
            _videoStream = _formatContext->streams[_streamIndex];
            break;
        }
    }
    if (_streamIndex == -1 || _videoStream == nullptr) {
        throw ghoul::RuntimeError(fmt::format("Failed to find video stream for {}", path));
    }

    // Find decoder
    _decoder = avcodec_find_decoder(_videoStream->codecpar->codec_id);
    if (!_decoder) {
        throw ghoul::RuntimeError(fmt::format("Failed to find decoder for {}", path));
    }

    // Find codec
    _codecContext = avcodec_alloc_context3(nullptr);
    int contextSuccess = avcodec_parameters_to_context(
        _codecContext,
        _videoStream->codecpar
    );
    if (contextSuccess < 0) {
        throw ghoul::RuntimeError(
            fmt::format("Failed to create codec context for {}", path)
        );
    }

    // Open the decoder
    if (avcodec_open2(_codecContext, _decoder, nullptr) < 0) {
        throw ghoul::RuntimeError(fmt::format("Failed to open codec for {}", path));
    }

    // Allocate the video frames
    _packet = av_packet_alloc();
    _avFrame = av_frame_alloc();    // Raw frame
    _glFrame = av_frame_alloc();    // Color-converted frame

    // Fill the destination frame for the convertion
    int glFrameSize = av_image_get_buffer_size(
        AV_PIX_FMT_RGB24,
        _codecContext->width,
        _codecContext->height,
        1
    );
    uint8_t* internalBuffer =
        reinterpret_cast<uint8_t*>(av_malloc(glFrameSize * sizeof(uint8_t)));
    av_image_fill_arrays(
        _glFrame->data,
        _glFrame->linesize,
        internalBuffer,
        AV_PIX_FMT_RGB24,
        _codecContext->width,
        _codecContext->height,
        1
    );

    // Update times
    _lastFrameTime = std::chrono::system_clock::now();

    return true;
}

bool ScreenSpaceVideoRenderable::deinitializeGL() {
    ScreenSpaceRenderable::deinitializeGL();
    return true;
}

void ScreenSpaceVideoRenderable::render() {

}

void ScreenSpaceVideoRenderable::update() {

    ScreenSpaceRenderable::update();
}

void ScreenSpaceVideoRenderable::bindTexture() {
    _texture->bind();
}
}
