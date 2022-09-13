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

#include <modules/exoplanetsexperttool/exoplanetsexperttoolmodule.h>

#include <modules/exoplanetsexperttool/rendering/renderableexoplanetglyphcloud.h>
#include <modules/exoplanetsexperttool/rendering/renderablepointdata.h>
#include <openspace/camera/camera.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/globalscallbacks.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/navigation/navigationhandler.h>
#include <openspace/scene/scene.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/scripting/scriptengine.h>
#include <openspace/util/factorymanager.h>
#include <openspace/query/query.h>
#include <ghoul/logging/logmanager.h>

namespace {
    constexpr const char _loggerCat[] = "ExoplanetsExpertToolModule";

    constexpr openspace::properties::Property::PropertyInfo EnabledInfo = {
        "Enabled",
        "Enabled",
        "Decides if the GUI for this module should be enabled"
    };

    constexpr openspace::properties::Property::PropertyInfo FilteredDataRowsInfo = {
        "FilteredDataRows",
        "Filtered Data Rows",
        "Contains the indices of the rows in the data file that are currently being "
        "shown in the tool.",
        openspace::properties::Property::Visibility::Hidden
    };

    struct [[codegen::Dictionary(ExoplanetsExpertToolModule)]] Parameters {
        // [[codegen::verbatim(EnabledInfo.description)]]
        std::optional<bool> enabled;
    };
#include "exoplanetsexperttoolmodule_codegen.cpp"
} // namespace

namespace openspace {

using namespace exoplanets;

ExoplanetsExpertToolModule::ExoplanetsExpertToolModule()
    : OpenSpaceModule(Name)
    , _enabled(EnabledInfo, false)
    , _filteredRows(FilteredDataRowsInfo)
    , _gui("ExoplanetsToolGui")
{
    addProperty(_enabled);
    addPropertySubOwner(_gui);

    _filteredRows.setReadOnly(true);
    addProperty(_filteredRows);

    global::callback::initialize->emplace_back([&]() {
        LDEBUG("Initializing Exoplanets Expert Tool GUI");
        _gui.initialize();
    });

    global::callback::deinitialize->emplace_back([&]() {
        LDEBUG("Deinitialize Exoplanets Expert Tool GUI");
        _gui.deinitialize();
    });

    global::callback::initializeGL->emplace_back([&]() {
        LDEBUG("Initializing Exoplanets Expert Tool GUI OpenGL");
        _gui.initializeGL();
    });

    global::callback::deinitializeGL->emplace_back([&]() {
        LDEBUG("Deinitialize Exoplanets Expert Tool GUI OpenGL");
        _gui.deinitializeGL();
    });

    global::callback::draw2D->emplace_back([&]() {
        if (!_enabled) {
            return;
        }

        WindowDelegate& delegate = *global::windowDelegate;
        const bool showGui = delegate.hasGuiWindow() ? delegate.isGuiWindow() : true;
        if (delegate.isMaster() && showGui) {
            const glm::ivec2 windowSize = delegate.currentSubwindowSize();
            const glm::ivec2 resolution = delegate.currentDrawBufferResolution();

            if (windowSize.x <= 0 || windowSize.y <= 0) {
                return;
            }

            const double dt = std::max(delegate.averageDeltaTime(), 0.0);
            // We don't do any collection of immediate mode user interface, so it
            // is fine to open and close a frame immediately
            _gui.startFrame(
                static_cast<float>(dt),
                glm::vec2(windowSize),
                resolution / windowSize,
                _mousePosition,
                _mouseButtons
            );

            _gui.endFrame();
        }
    });

    global::callback::keyboard->emplace_back(
        [&](Key key, KeyModifier mod, KeyAction action, bool isGuiWindow) -> bool {
            if (!_enabled || !isGuiWindow) {
                return false;
            }
            return _gui.keyCallback(key, mod, action);
        }
    );

    global::callback::character->emplace_back(
        [&](unsigned int codepoint, KeyModifier modifier, bool isGuiWindow) -> bool {
            if (!isGuiWindow) {
                return false;
            }
            return _gui.charCallback(codepoint, modifier);
        }
    );

    global::callback::mousePosition->emplace_back(
        [&](double x, double y, bool isGuiWindow) {
            if (!_enabled || !isGuiWindow) {
                return; // do nothing
            }
            _mousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
        }
    );

    global::callback::mouseButton->emplace_back(
        [&](MouseButton button, MouseAction action,
            KeyModifier, bool isGuiWindow) -> bool
        {
            if (!_enabled || !isGuiWindow) {
                return false;
            }

            if (action == MouseAction::Press) {
                _mouseButtons |= (1 << static_cast<int>(button));
            }
            else if (action == MouseAction::Release) {
                _mouseButtons &= ~(1 << static_cast<int>(button));
            }

            return _gui.mouseButtonCallback(button, action);
        }
    );

    global::callback::mouseScrollWheel->emplace_back(
        [&](double, double posY, bool isGuiWindow) -> bool {
            if (!_enabled || !isGuiWindow) {
                return false;
            }
            return _gui.mouseWheelCallback(posY);
        }
    );

    // Update renderbin based on distance, to avoid funky rendering with milky way volume
    // and still get correct depth informaiton when close to planet systems
    // OBS! This is kind of ugly, imo. Would have been nice to do it with events instead
    global::callback::preSync->emplace_back([this]() {
        if (!_enabled) {
            return;
        }

        constexpr const double ThreshHoldDistance = 1.0e19;

        // TODO: this should be done for any current focus?
        SceneGraphNode* rootNode = sceneGraphNode("Root");
        if (!rootNode) {
            return;
        }
        const glm::dvec3 cameraPos = global::navigationHandler->camera()->positionVec3();
        const glm::dvec3 sunPos = rootNode->worldPosition();

        const double distance = glm::distance(cameraPos, sunPos);

        bool cameraIsInGalaxy = distance < ThreshHoldDistance;
        if (cameraIsInGalaxy != _cameraWasWithinGalaxy) {
            openspace::global::scriptEngine->queueScript(
                fmt::format(
                    "openspace.setPropertyValueSingle('{}', {})",
                    fmt::format("Scene.{}.Renderable.RenderBinMode", GlyphCloudIdentifier),
                    cameraIsInGalaxy ? 2 : 3 //"PreDeferredTransparent" : "PostDeferredTransparent"
                ),
                scripting::ScriptEngine::RemoteScripting::Yes
            );
        }
        _cameraWasWithinGalaxy = cameraIsInGalaxy;
    });
}

bool ExoplanetsExpertToolModule::enabled() const {
    return _enabled;
}

void ExoplanetsExpertToolModule::internalInitialize(const ghoul::Dictionary& dict) {
    const Parameters p = codegen::bake<Parameters>(dict);
    _enabled = p.enabled.value_or(false);

    auto fRenderable = FactoryManager::ref().factory<Renderable>();
    ghoul_assert(fRenderable, "No renderable factory existed");
    fRenderable->registerClass<RenderableExoplanetGlyphCloud>(
        "RenderableExoplanetGlyphCloud"
    );
    fRenderable->registerClass<RenderablePointData>("RenderablePointData");
}

std::vector<documentation::Documentation>
ExoplanetsExpertToolModule::documentations() const
{
    return {
        RenderableExoplanetGlyphCloud::Documentation(),
        RenderablePointData::Documentation()
    };
}

} // namespace openspace
