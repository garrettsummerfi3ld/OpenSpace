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

#pragma once

#include "openspace/properties/listproperty.h"
#include "openspace/properties/optionproperty.h"
#include "openspace/properties/selectionproperty.h"
#include <openspace/rendering/renderable.h>

#include <openspace/properties/list/stringlistproperty.h>
#include <openspace/properties/list/intlistproperty.h>
#include <openspace/properties/list/doublelistproperty.h>
#include <openspace/properties/vector/dvec3property.h>


#include <md_gl.h>
#include <md_molecule.h>
#include <md_trajectory.h>

namespace openspace {

struct RenderData;

class RenderableSimulationBox : public Renderable {
public:
    explicit RenderableSimulationBox(const ghoul::Dictionary& dictionary);
    virtual ~RenderableSimulationBox();

    void initialize() override;
    void initializeGL() override;
    void deinitializeGL() override;
    bool isReady() const override;
    void render(const RenderData& data, RendererTasks& tasks) override;
    void update(const UpdateData& data) override;

    static documentation::Documentation Documentation();

private:
    struct molecule_state_t {
        glm::dvec3 position;
        double angle;
        glm::dvec3 direction;     // moving direction where magnitude is linear velocity
        glm::dvec3 rotationAxis;  // rotation axis where magnitude is angular velocity
    };

    struct molecule_data_t {
        std::vector<molecule_state_t> states;
        md_molecule_t molecule;
        const md_trajectory_i* trajectory;
        md_gl_representation_t drawRep;
        md_gl_molecule_t drawMol;
    };
    
    void updateSimulation(molecule_data_t& mol, double dt);
    
    void initMolecule(molecule_data_t& mol, std::string_view molFile, std::string_view trajFile = {});
    void freeMolecule(molecule_data_t& mol);

    bool _renderableInView; // indicates whether the molecule is in view in any camera's viewpoint

    double _frame;
    
    std::vector<molecule_data_t> _molecules;
    
    properties::StringListProperty _moleculeFiles;
    properties::StringListProperty _trajectoryFiles;
    properties::OptionProperty _repType;
    properties::OptionProperty _coloring;
    properties::FloatProperty _repScale;
    properties::FloatProperty _animationSpeed;
    properties::FloatProperty _simulationSpeed;
    properties::IntListProperty _moleculeCounts;
    properties::FloatProperty _linearVelocity;
    properties::FloatProperty _angularVelocity;
    properties::DVec3Property _simulationBox;
    properties::FloatProperty _collisionRadius;
    properties::StringProperty _viamdFilter;

    properties::BoolProperty  _ssaoEnabled;
    properties::FloatProperty _ssaoIntensity;
    properties::FloatProperty _ssaoRadius;
    properties::FloatProperty _ssaoBias;
    properties::FloatProperty _exposure;
};

} // namespace openspace