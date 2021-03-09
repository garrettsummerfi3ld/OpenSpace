/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
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

#ifndef __OPENSPACE_MODULE_AIRTRAFFIC___RENDERABLEAIRTRAFFIC___H__
#define __OPENSPACE_MODULE_AIRTRAFFIC___RENDERABLEAIRTRAFFIC___H__

#include <openspace/rendering/renderable.h>
#include <ghoul/opengl/uniformcache.h>
#include <openspace/json.h>
#include <openspace/util/httprequest.h>
#include <openspace/util/time.h>
#include <future>
#include <list>

namespace ghoul::filesystem { class File; }
namespace ghoul::opengl {
    class ProgramObject;
    class Texture;
} // namespace ghoul::opengl

namespace openspace {

namespace documentation { struct Documentation; }
using json = nlohmann::json;
using namespace std::chrono_literals;

class RenderableAirTraffic : public Renderable {
public:
    explicit RenderableAirTraffic(const ghoul::Dictionary& dictionary);
    virtual ~RenderableAirTraffic() = default;

    void initializeGL() override;
    void deinitializeGL() override;

    json fetchData();

    json parseData(SyncHttpMemoryDownload& response);

    bool isReady() const override;

    void render(const RenderData& data, RendererTasks& rendererTask) override;

    void updateBuffers();
    
    static documentation::Documentation Documentation();

private:

    static const int _TRAILSIZE = 10;
    static const int _THRESHOLD = -9999;
    const float _LINEWIDTH = std::ceil((2.f + 8.f) * std::sqrt(2.f)); 

    struct AircraftVBOLayout {
        float latitude = static_cast<float>(_THRESHOLD);
        float longitude = static_cast<float>(_THRESHOLD);
        float barometricAltitude = 0.f;
        float velocity = 0.f; 
        float flightDirection = 0.f; // true_track in data
        int lastContact = 0; // Draw only if changed
    };
    
    template<size_t N>
    struct aircraftList {
        aircraftList() : list(N) {}
        std::list<AircraftVBOLayout> list;
    };

    // Backend storage for vertex buffer object containing all points
    std::vector<AircraftVBOLayout>  _vertexBufferData;
   
    std::unique_ptr<ghoul::opengl::ProgramObject> _shader = nullptr;

    UniformCache(modelViewProjection, trailSize, resolution, lineWidth) _uniformCache;

    GLuint _vertexArray = 0;
    GLuint _vertexBuffer = 0;
    std::future<json> _fut;
    bool _isDataLoading = false;
    properties::FloatProperty _pointSize;
    json _data = json({});
    std::map<std::string, aircraftList<_TRAILSIZE>> _aircraftMap;
    
    // Fix secure way to handle credentials
    //const std::string _url = "https://" + _PASSWORD + ":" + _USERNAME + "@opensky-network.org/api/states/all";
    const std::string _url = "https://opensky-network.org/api/states/all";
    double _deltaTime = Time::now().j2000Seconds();
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_AIRTRAFFIC___RENDERABLEAIRTRAFFIC___H__
