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

#ifndef __OPENSPACE_MODULE_SONIFICATION___COMPARESONIFICATION___H__
#define __OPENSPACE_MODULE_SONIFICATION___COMPARESONIFICATION___H__

#include <modules/sonification/include/sonificationbase.h>

#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/optionproperty.h>

namespace openspace {

class CompareSonification : public SonificationBase {
public:
    CompareSonification(const std::string& ip, int port);
    virtual ~CompareSonification() override;

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
    const int NumDataItems = 3;
    const int DistanceIndex = 0;
    const int HAngleIndex = 1;
    const int VAngleIndex = 2;

    // Struct to hold data for all the planets
    struct Planet {
        Planet(std::string id = "") {
            identifier = id;
        }

        std::string identifier;

        // Distance, horizontal angle, vertical angle
        std::vector<double> data = std::vector<double>(3);

        // <name of moon, <distance, horizontal angle, vertical angle>>
        std::vector<std::pair<std::string, std::vector<double>>> moons;
    };

    /**
     * Update distance and angle data for the given planet
     *
     * \param camera pointer to the camera in the scene. Used to calculated the data for
     *               the planet
     * \param planet a reference to the internally stored planet data that should be
     *               updated
     *
     * \return true if the data is new compared to before, otherwise false
     */
    bool getData(const Camera* camera, Planet& planet);

    /**
     * Create a osc::Blob object with current sonification settings.
     * Order of settings: size/day, gravity, temperature, atmosphere, moons, rings
     *
     * \return a osc::Blob object with current sonificaiton settings
     */
    osc::Blob createSettingsBlob() const;

    /**
     * Send current sonification settings over the osc connection
     * Order of data: name of first planet, name of second planet, settings
     */
    void sendSettings();


    /**
     * Function that gets called when either the first or second planet selection
     * was changed
     *
     * \param changedPlanet the planet that was recently changed
     * \param notChangedPlanet the planet that was NOT changed
     * \param prevChangedPlanet the previous value of the planet that was recently changed
     */
    void planetSelectionChanged(properties::OptionProperty& changedPlanet,
        properties::OptionProperty& notChangedPlanet, std::string& prevChangedPlanet);

    // Properties onChange
    void onFirstChanged();
    void onSecondChanged();
    void onToggleAllChanged();

    double _anglePrecision;
    double _distancePrecision;

    float _focusScale = 2000.f;
    std::vector<Planet> _planets;
    std::string _oldFirst;
    std::string _oldSecond;

    // Properties
    properties::OptionProperty _firstPlanet;
    properties::OptionProperty _secondPlanet;

    properties::BoolProperty _toggleAll;
    properties::BoolProperty _sizeDayEnabled;
    properties::BoolProperty _gravityEnabled;
    properties::BoolProperty _temperatureEnabled;
    properties::BoolProperty _atmosphereEnabled;
    properties::BoolProperty _moonsEnabled;
    properties::BoolProperty _ringsEnabled;
};

} // namespace openspace

#endif __OPENSPACE_MODULE_SONIFICATION___COMPARESONIFICATION___H__
