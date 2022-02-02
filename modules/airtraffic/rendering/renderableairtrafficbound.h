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

#ifndef __OPENSPACE_MODULE_AIRTRAFFIC___RENDERABLEAIRTRAFFICBOUND___H__
#define __OPENSPACE_MODULE_AIRTRAFFIC___RENDERABLEAIRTRAFFICBOUND___H__

#include <openspace/rendering/renderable.h>

#include <openspace/util/time.h>
#include <openspace/properties/optionproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/vector/vec3property.h>
#include <openspace/properties/vector/vec2property.h>
#include <ghoul/opengl/uniformcache.h>

namespace ghoul::filesystem { class File; }

namespace ghoul::opengl {
    class ProgramObject;
    class Texture;
} // namespace ghoul::opengl

namespace openspace {

namespace documentation { struct Documentation; }

class RenderableAirTrafficBound : public Renderable {
public:
    explicit RenderableAirTrafficBound(const ghoul::Dictionary& dictionary);
    virtual ~RenderableAirTrafficBound() = default;

    void initializeGL() override;
    void deinitializeGL() override;

    bool isReady() const override;

    void render(const RenderData& data, RendererTasks& rendererTask) override;

    static glm::vec2 getLatBound();
    static glm::vec2 getLonBound();

    void updateBuffers();
    
    static documentation::Documentation Documentation();

private:
    properties::FloatProperty _lineWidth;
    properties::Vec3Property _color;
    properties::FloatProperty _opacity;
    properties::Vec2Property _latitudeThreshold;
    properties::Vec2Property _longitudeThreshold;

    static glm::vec2 _lat;
    static glm::vec2 _lon;

    struct BoundVBOLayout {
        float latitude = 0.f;
        float longitude = 0.f;
    };

    // Backend storage for vertex buffer object containing all points
    std::vector<BoundVBOLayout>  _vertexBufferData;
   
    std::unique_ptr<ghoul::opengl::ProgramObject> _shader = nullptr;

    UniformCache(modelViewProjection, color, opacity, latitudeThreshold,
        longitudeThreshold, cameraPosition, modelTransform, clipping) _uniformCache;

    GLuint _vertexArray = 0;
    GLuint _vertexBuffer = 0;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_AIRTRAFFIC___RENDERABLEAIRTRAFFICBOUND___H__