/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2023                                                               *
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

#ifndef __OPENSPACE_MODULE_SONIFICATION___SOLARSONIFICATION___H__
#define __OPENSPACE_MODULE_SONIFICATION___SOLARSONIFICATION___H__

#include <modules/sonification/include/sonificationbase.h>

#include <openspace/properties/scalar/boolproperty.h>

namespace openspace {

class SolarSonification : public SonificationBase {
public:
    SolarSonification(const std::string& ip, int port);
    virtual ~SolarSonification() override;

    /**
     * Main update function for the sonification
     *
     * \param camera pointer to the camera in the scene
     */
    virtual void update(const Camera* camera) override;

    /**
     * Function to stop the sonification
     */
    virtual void stop() override;

private:
    // Indices for the planets
    const int NumPlanets = 8;
    const int MercuryIndex = 0;
    const int VenusIndex = 1;
    const int EarthIndex = 2;
    const int MarsIndex = 3;
    const int JupiterIndex = 4;
    const int SaturnIndex = 5;
    const int UranusIndex = 6;
    const int NeptuneIndex = 7;

    /**
     * Create a vector with current sonification settings.
     * Order of settings: Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune
     *
     * \return a osc::Blob object with current sonificaiton settings
     */
    std::vector<int> createSettingsVector() const;

    /**
     * Send current sonification settings over the osc connection
     */
    void sendSettings();

    // Properties onChange
    void onEnabledChanged();
    void onToggleAllChanged();

    // Properties
    properties::BoolProperty _toggleAll;
    properties::BoolProperty _mercuryEnabled;
    properties::BoolProperty _venusEnabled;
    properties::BoolProperty _earthEnabled;
    properties::BoolProperty _marsEnabled;
    properties::BoolProperty _jupiterEnabled;
    properties::BoolProperty _saturnEnabled;
    properties::BoolProperty _uranusEnabled;
    properties::BoolProperty _neptuneEnabled;
};

} // namespace openspace

#endif __OPENSPACE_MODULE_SONIFICATION___SOLARSONIFICATION___H__
