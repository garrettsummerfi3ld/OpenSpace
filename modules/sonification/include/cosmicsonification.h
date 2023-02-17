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

#ifndef __OPENSPACE_MODULE_SONIFICATION___COSMICSONIFICATION___H__
#define __OPENSPACE_MODULE_SONIFICATION___COSMICSONIFICATION___H__

#include <modules/sonification/include/sonificationbase.h>

#include <map>

namespace openspace {

class CosmicSonification : public SonificationBase {

public:
    CosmicSonification(const std::string& ip, int port);
    virtual ~CosmicSonification();

    virtual void update(const Scene* scene, const Camera* camera) override;

private:

    double _anglePrecision;
    double _distancePrecision;

    // std::map<<Identifier, <distance, angle>>>
    std::map<std::string, std::vector<double>> _nodeData;
};

} // openspace namespace

#endif // __OPENSPACE_MODULE_SONIFICATION___COSMICSONIFICATION___H__
