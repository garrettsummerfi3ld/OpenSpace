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

#include <modules/airtraffic/airtrafficmodule.h>

#include <modules/airtraffic/rendering/renderableairtrafficlive.h>
#include <modules/airtraffic/rendering/renderableairtraffichistorical.h>
#include <modules/airtraffic/rendering/renderableairtrafficbound.h>
#include <openspace/rendering/renderable.h>
#include <openspace/util/factorymanager.h>
#include <ghoul/misc/assert.h>

namespace openspace {

AirTrafficModule::AirTrafficModule() : OpenSpaceModule(Name) {}

void AirTrafficModule::internalInitialize(const ghoul::Dictionary&) {
    auto fRenderable = FactoryManager::ref().factory<Renderable>();
    ghoul_assert(fRenderable, "No renderable factory existed");

    fRenderable->registerClass<RenderableAirTrafficLive>("RenderableAirTrafficLive");
    fRenderable->registerClass<RenderableAirTrafficHistorical>(
        "RenderableAirTrafficHistorical"
    );
    fRenderable->registerClass<RenderableAirTrafficBound>("RenderableAirTrafficBound");
}

std::vector<documentation::Documentation> AirTrafficModule::documentations() const {
    return {
        RenderableAirTrafficLive::Documentation(),
        RenderableAirTrafficHistorical::Documentation(),
        RenderableAirTrafficBound::Documentation()
    };
}

} // namespace openspace
