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

#include <modules/fieldlinessequence/util/fieldlinesstate.h>

#include <openspace/json.h>
#include <openspace/util/time.h>
#include <ghoul/fmt.h>
#include <ghoul/logging/logmanager.h>
#include <fstream>
#include <iomanip>

namespace {
    constexpr const char* _loggerCat = "FieldlinesState";
    constexpr const int CurrentVersion = 0;
    using json = nlohmann::json;
} // namespace

namespace openspace {

/**
 * Converts all glm::vec3 in _vertexPositions from spherical (radius, latitude, longitude)
 * coordinates into cartesian coordinates. The longitude and latitude coordinates are
 * expected to be in degrees. scale is an optional scaling factor.
 */
void FieldlinesState::convertLatLonToCartesian(float scale) {
    for (glm::vec3& p : _vertexPositions) {
        const float r = p.x * scale;
        const float lat = glm::radians(p.y);
        const float lon = glm::radians(p.z);
        const float rCosLat = r * cos(lat);

        p = glm::vec3(rCosLat * cos(lon), rCosLat* sin(lon), r * sin(lat));
    }
}

void FieldlinesState::scalePositions(float scale) {
    for (glm::vec3& p : _vertexPositions) {
        p *= scale;
    }
}

bool FieldlinesState::loadStateFromOsfls(const std::string& pathToOsflsFile) {
    std::ifstream ifs(pathToOsflsFile, std::ifstream::binary);
    if (!ifs.is_open()) {
        LERROR("Couldn't open file: " + pathToOsflsFile);
        return false;
    }

    int binFileVersion;
    ifs.read(reinterpret_cast<char*>(&binFileVersion), sizeof(int));

    switch (binFileVersion) {
        case 0:
            // No need to put everything in this scope now, as only version 0 exists!
            break;
        default:
            LERROR("VERSION OF BINARY FILE WAS NOT RECOGNIZED!");
            return false;
    }

    // Define tmp variables to store meta data in
    size_t nLines;
    size_t nPoints;
    size_t nExtras;
    size_t byteSizeAllNames;

    // Read single value variables
    ifs.read(reinterpret_cast<char*>(&_triggerTime), sizeof(double));
    ifs.read(reinterpret_cast<char*>(&_model), sizeof(int32_t));
    ifs.read(reinterpret_cast<char*>(&_isMorphable), sizeof(bool));
    ifs.read(reinterpret_cast<char*>(&nLines), sizeof(uint64_t));
    ifs.read(reinterpret_cast<char*>(&nPoints), sizeof(uint64_t));
    ifs.read(reinterpret_cast<char*>(&nExtras), sizeof(uint64_t));
    ifs.read(reinterpret_cast<char*>(&byteSizeAllNames), sizeof(uint64_t));

    _lineStart.resize(nLines);
    _lineCount.resize(nLines);
    _vertexPositions.resize(nPoints);
    _extraQuantities.resize(nExtras);
    _extraQuantityNames.resize(nExtras);

    // Read vertex position data
    ifs.read(reinterpret_cast<char*>(_lineStart.data()), sizeof(int32_t) * nLines);
    ifs.read(reinterpret_cast<char*>(_lineCount.data()), sizeof(uint32_t) * nLines);
    ifs.read(
        reinterpret_cast<char*>(_vertexPositions.data()),
        3 * sizeof(float) * nPoints
    );

    // Read all extra quantities
    for (std::vector<float>& vec : _extraQuantities) {
        vec.resize(nPoints);
        ifs.read(reinterpret_cast<char*>(vec.data()), sizeof(float) * nPoints);
    }

    // Read all extra quantities' names. Stored as multiple c-strings
    std::string allNamesInOne;
    std::vector<char> buffer(byteSizeAllNames);
    ifs.read(buffer.data(), byteSizeAllNames);
    allNamesInOne.assign(buffer.data(), byteSizeAllNames);

    size_t offset = 0;
    for (size_t i = 0; i < nExtras; ++i) {
        auto endOfVarName = allNamesInOne.find('\0', offset);
        endOfVarName -= offset;
        const std::string varName = allNamesInOne.substr(offset, endOfVarName);
        offset += varName.size() + 1;
        _extraQuantityNames[i] = varName;
    }

    return true;
}

bool FieldlinesState::loadStateFromJson(const std::string& pathToJsonFile,
                                        fls::Model Model, float coordToMeters)
{

    // --------------------- ENSURE FILE IS VALID, THEN PARSE IT --------------------- //
    std::ifstream ifs(pathToJsonFile);

    if (!ifs.is_open()) {
        LERROR(fmt::format("FAILED TO OPEN FILE: {}", pathToJsonFile));
        return false;
    }

    json jFile;
    ifs >> jFile;
    // -------------------------------------------------------------------------------- //

    _model = Model;

    const char* sData  = "data";
    const char* sTrace = "trace";

    // ----- EXTRACT THE EXTRA QUANTITY NAMES & TRIGGER TIME (same for all lines) ----- //
    {
        const char* sTime = "time";
        const json& jTmp = *(jFile.begin()); // First field line in the file
        _triggerTime = Time::convertTime(jTmp[sTime]);

        const char* sColumns = "columns";
        const json::value_type& variableNameVec = jTmp[sTrace][sColumns];
        const size_t nVariables = variableNameVec.size();
        const size_t nPosComponents = 3; // x,y,z

        if (nVariables < nPosComponents) {
            LERROR(
                pathToJsonFile + ": Each field '" + sColumns +
                "' must contain the variables: 'x', 'y' and 'z' (order is important)."
            );
            return false;
        }

        for (size_t i = nPosComponents ; i < nVariables ; ++i) {
            _extraQuantityNames.push_back(variableNameVec[i]);
        }
    }

    const size_t nExtras = _extraQuantityNames.size();
    _extraQuantities.resize(nExtras);

    size_t lineStartIdx = 0;
    // Loop through all fieldlines
    for (json::iterator lineIter = jFile.begin(); lineIter != jFile.end(); ++lineIter) {
        // The 'data' field in the 'trace' variable contains all vertex positions and the
        // extra quantities. Each element is an array related to one vertex point.
        const std::vector<std::vector<float>>& jData = (*lineIter)[sTrace][sData];
        const size_t nPoints = jData.size();

        for (size_t j = 0; j < nPoints; ++j) {
            const std::vector<float>& variables = jData[j];

            // Expects the x, y and z variables to be stored first!
            const size_t xIdx = 0;
            const size_t yIdx = 1;
            const size_t zIdx = 2;
            _vertexPositions.push_back(
                coordToMeters * glm::vec3(
                    variables[xIdx],
                    variables[yIdx],
                    variables[zIdx]
                )
            );

            // Add the extra quantites. Stored in the same array as the x,y,z variables.
            // Hence index of the first extra quantity = 3
            for (size_t xtraIdx = 3, k = 0 ; k < nExtras; ++k, ++xtraIdx) {
                _extraQuantities[k].push_back(variables[xtraIdx]);
            }
        }
        _lineCount.push_back(static_cast<GLsizei>(nPoints));
        _lineStart.push_back(static_cast<GLsizei>(lineStartIdx));
        lineStartIdx += nPoints;
    }
    return true;
}

/**
 * \param absPath must be the path to the file (incl. filename but excl. extension!)
 * Directory must exist! File is created (or overwritten if already existing).
 * File is structured like this: (for version 0)
 *  0. int                    - version number of binary state file! (in case something
 *                              needs to be altered in the future, then increase
 *                              CurrentVersion)
 *  1. double                 - _triggerTime
 *  2. int                    - _model
 *  3. bool                   - _isMorphable
 *  4. size_t                 - Number of lines in the state  == _lineStart.size()
 *                                                            == _lineCount.size()
 *  5. size_t                 - Total number of vertex points == _vertexPositions.size()
 *                                                           == _extraQuantities[i].size()
 *  6. size_t                 - Number of extra quantites     == _extraQuantities.size()
 *                                                           == _extraQuantityNames.size()
 *  7. site_t                 - Number of total bytes that ALL _extraQuantityNames
 *                              consists of (Each such name is stored as a c_str which
 *                              means it ends with the null char '\0' )
 *  7. std::vector<GLint>     - _lineStart
 *  8. std::vector<GLsizei>   - _lineCount
 *  9. std::vector<glm::vec3> - _vertexPositions
 * 10. std::vector<float>     - _extraQuantities
 * 11. array of c_str         - Strings naming the extra quantities (elements of
 *                              _extraQuantityNames). Each string ends with null char '\0'
 */
void FieldlinesState::saveStateToOsfls(const std::string& absPath) {
    // ------------------------------- Create the file ------------------------------- //
    std::string pathSafeTimeString = std::string(Time(_triggerTime).ISO8601());
    pathSafeTimeString.replace(13, 1, "-");
    pathSafeTimeString.replace(16, 1, "-");
    pathSafeTimeString.replace(19, 1, "-");
    const std::string& fileName = pathSafeTimeString + ".osfls";

    std::ofstream ofs(absPath + fileName, std::ofstream::binary | std::ofstream::trunc);
    if (!ofs.is_open()) {
        LERROR(fmt::format(
            "Failed to save state to binary file: {}{}", absPath, fileName
        ));
        return;
    }

    // --------- Add each string of _extraQuantityNames into one long string ---------- //
    std::string allExtraQuantityNamesInOne = "";
    for (const std::string& str : _extraQuantityNames) {
        allExtraQuantityNamesInOne += str + '\0'; // Add null char '\0' for easier reading
    }

    const size_t nLines = _lineStart.size();
    const size_t nPoints = _vertexPositions.size();
    const size_t nExtras = _extraQuantities.size();
    const size_t nStringBytes = allExtraQuantityNamesInOne.size();

    //----------------------------- WRITE EVERYTHING TO FILE -----------------------------
    // VERSION OF BINARY FIELDLINES STATE FILE - IN CASE STRUCTURE CHANGES IN THE FUTURE
    ofs.write(reinterpret_cast<const char*>(&CurrentVersion), sizeof(int));

    //-------------------- WRITE META DATA FOR STATE --------------------------------
    ofs.write(reinterpret_cast<const char*>(&_triggerTime), sizeof(_triggerTime));
    ofs.write(reinterpret_cast<const char*>(&_model), sizeof(int32_t));
    ofs.write(reinterpret_cast<const char*>(&_isMorphable), sizeof(bool));

    ofs.write(reinterpret_cast<const char*>(&nLines), sizeof(uint64_t));
    ofs.write(reinterpret_cast<const char*>(&nPoints), sizeof(uint64_t));
    ofs.write(reinterpret_cast<const char*>(&nExtras), sizeof(uint64_t));
    ofs.write(reinterpret_cast<const char*>(&nStringBytes), sizeof(uint64_t));

    //---------------------- WRITE ALL ARRAYS OF DATA --------------------------------
    ofs.write(reinterpret_cast<char*>(_lineStart.data()), sizeof(int32_t) * nLines);
    ofs.write(reinterpret_cast<char*>(_lineCount.data()), sizeof(uint32_t) * nLines);
    ofs.write(
        reinterpret_cast<char*>(_vertexPositions.data()),
        3 * sizeof(float) * nPoints
    );
    // Write the data for each vector in _extraQuantities
    for (std::vector<float>& vec : _extraQuantities) {
        ofs.write(reinterpret_cast<char*>(vec.data()), sizeof(float) * nPoints);
    }
    ofs.write(allExtraQuantityNamesInOne.c_str(), nStringBytes);
}

// TODO: This should probably be rewritten, but this is the way the files were structured
// by CCMC
// Structure of File! NO TRAILING COMMAS ALLOWED!
// Additional info can be stored within each line as the code only extracts the keys it
// needs (time, trace & data)
// The key/name of each line ("0" & "1" in the example below) is arbitrary
// {
//     "0":{
//         "time": "YYYY-MM-DDTHH:MM:SS.XXX",
//         "trace": {
//             "columns": ["x","y","z","s","temperature","rho","j_para"],
//             "data": [[8.694,127.853,115.304,0.0,0.047,9.249,-5e-10],...,
//                     [8.698,127.253,114.768,0.800,0.0,9.244,-5e-10]]
//         },
//     },
//     "1":{
//         "time": "YYYY-MM-DDTHH:MM:SS.XXX
//         "trace": {
//             "columns": ["x","y","z","s","temperature","rho","j_para"],
//             "data": [[8.694,127.853,115.304,0.0,0.047,9.249,-5e-10],...,
//                     [8.698,127.253,114.768,0.800,0.0,9.244,-5e-10]]
//         },
//     }
// }
void FieldlinesState::saveStateToJson(const std::string& absPath) {
    // Create the file
    const char* ext = ".json";
    std::ofstream ofs(absPath + ext, std::ofstream::trunc);
    if (!ofs.is_open()) {
        LERROR(fmt::format(
            "Failed to save state to json file at location: {}{}", absPath, ext
        ));
        return;
    }
    LINFO(fmt::format("Saving fieldline state to: {}{}", absPath, ext));

    json jColumns = { "x", "y", "z" };
    for (const std::string& s : _extraQuantityNames) {
        jColumns.push_back(s);
    }

    json jFile;

    std::string_view timeStr = Time(_triggerTime).ISO8601();
    const size_t nLines = _lineStart.size();
    // const size_t nPoints      = _vertexPositions.size();
    const size_t nExtras = _extraQuantities.size();

    size_t pointIndex = 0;
    for (size_t lineIndex = 0; lineIndex < nLines; ++lineIndex) {
        json jData = json::array();
        for (GLsizei i = 0; i < _lineCount[lineIndex]; i++, ++pointIndex) {
            const glm::vec3 pos = _vertexPositions[pointIndex];
            json jDataElement = { pos.x, pos.y, pos.z };

            for (size_t extraIndex = 0; extraIndex < nExtras; ++extraIndex) {
                jDataElement.push_back(_extraQuantities[extraIndex][pointIndex]);
            }
            jData.push_back(jDataElement);
        }

        jFile[std::to_string(lineIndex)] = {
            { "time", timeStr },
            { "trace", {
                { "columns", jColumns },
                { "data", jData }
            }}
        };
    }

    //----------------------------- WRITE EVERYTHING TO FILE -----------------------------
    const int indentationSpaces = 2;
    ofs << std::setw(indentationSpaces) << jFile << std::endl;

    LINFO(fmt::format("Saved fieldline state to: {}{}", absPath, ext));
}

void FieldlinesState::setModel(fls::Model m) {
    _model = m;
}

void FieldlinesState::setTriggerTime(double t) {
    _triggerTime = t;
}

// Returns one of the extra quantity vectors, _extraQuantities[index].
// If index is out of scope an empty vector is returned and the referenced bool is false.
std::vector<float> FieldlinesState::extraQuantity(size_t index, bool& isSuccessful) const
{
    if (index < _extraQuantities.size()) {
        isSuccessful = true;
        return _extraQuantities[index];
    }
    else {
        isSuccessful = false;
        LERROR("Provided Index was out of scope!");
        return {};
    }
}

// Moves the points in @param line over to _vertexPositions and updates
// _lineStart & _lineCount accordingly.

void FieldlinesState::addLine(std::vector<glm::vec3>& line) {
    const size_t nNewPoints = line.size();
    const size_t nOldPoints = _vertexPositions.size();
    _lineStart.push_back(static_cast<GLint>(nOldPoints));
    _lineCount.push_back(static_cast<GLsizei>(nNewPoints));
    _vertexPositions.reserve(nOldPoints + nNewPoints);
    _vertexPositions.insert(
        _vertexPositions.end(),
        std::make_move_iterator(line.begin()),
        std::make_move_iterator(line.end())
    );
    line.clear();
}

void FieldlinesState::appendToExtra(size_t idx, float val) {
    _extraQuantities[idx].push_back(val);
}

void FieldlinesState::moveLine()
{
    LDEBUG(fmt::format(
        "BEFORE: {} {} {}",
        vertexPositions()[0].x,
        vertexPositions()[0].y,
        vertexPositions()[0].z));

    for (glm::vec3& vertex : _vertexPositions) {
        vertex.x -= 100000.0f;
    }

    LDEBUG(fmt::format(
        "AFTER:  {} {} {}",
        vertexPositions()[0].x,
        vertexPositions()[0].y,
        vertexPositions()[0].z));
}

FieldlinesState::cartesianTrace(const std::string& variable, const float& startComponent1,
    const float& startComponent2, const float& startComponent3, Interpolator* interpolator,
    const Direction& dir)
{

    float adjusted_dn = dn;
    if (dir == REVERSE)
    {
        adjusted_dn = -dn;
    }
#ifdef DEBUG_TRACER
    cerr << "entered cartesianTrace" << endl;
    cerr << "variable: " << variable << endl;
#endif
    Point3f min;//point representing the min values of the three dimensions;
    Point3f max;//point representing the max values of the three dimensions;
    std::string model_name = kameleon->getModelName();
    float arcLength = 0.f;

    if (!useROI)
    {

        if (model_name == ccmc::strings::models::batsrus_)
        {

#ifdef DEBUG_TRACER
            cerr << "inside. model_name: \"" << model_name << "\"" << endl;
#endif

            min.component1 = (kameleon->getGlobalAttribute(ccmc::strings::attributes::global_x_min_)).getAttributeFloat();
            min.component2 = (kameleon->getGlobalAttribute(ccmc::strings::attributes::global_y_min_)).getAttributeFloat();
            min.component3 = (kameleon->getGlobalAttribute(ccmc::strings::attributes::global_z_min_)).getAttributeFloat();

            max.component1 = (kameleon->getGlobalAttribute(ccmc::strings::attributes::global_x_max_)).getAttributeFloat();
            max.component2 = (kameleon->getGlobalAttribute(ccmc::strings::attributes::global_y_max_)).getAttributeFloat();
            max.component3 = (kameleon->getGlobalAttribute(ccmc::strings::attributes::global_z_max_)).getAttributeFloat();
            //std::cout << "Tracer min: " << min << " max: " << max << std::endl;

        }
        else
        {

            if (model_name == ccmc::strings::models::ucla_ggcm_ || model_name == ccmc::strings::models::open_ggcm_)
            {
                /** the signs of x and y are flipped **/

                /*min.component1 = -1.0 * *(float *) vattribute_get(ccmc::strings::variables::x_, "actual_max");
                 min.component2 = -1.0 * *(float *) vattribute_get(ccmc::strings::variables::y_, "actual_max");
                 min.component3 = *(float *) vattribute_get(ccmc::strings::variables::z_, ccmc::strings::attributes::actual_min_);

                 max.component1 = -1.0 * *(float *) vattribute_get(ccmc::strings::variables::x_, ccmc::strings::attributes::actual_min_);
                 max.component2 = -1.0 * *(float *) vattribute_get(ccmc::strings::variables::y_, ccmc::strings::attributes::actual_min_);
                 max.component3 = *(float *) vattribute_get(ccmc::strings::variables::z_, "actual_max");*/
                min.component1 = -1.0 * (kameleon->getVariableAttribute(ccmc::strings::variables::x_, ccmc::strings::attributes::actual_max_)).getAttributeFloat();
                min.component2 = -1.0 * (kameleon->getVariableAttribute(ccmc::strings::variables::y_, ccmc::strings::attributes::actual_max_)).getAttributeFloat();
                min.component3 = (kameleon->getVariableAttribute(ccmc::strings::variables::z_, ccmc::strings::attributes::actual_min_)).getAttributeFloat();

                max.component1 = -1.0 * (kameleon->getVariableAttribute(ccmc::strings::variables::x_, ccmc::strings::attributes::actual_min_)).getAttributeFloat();
                max.component2 = -1.0 * (kameleon->getVariableAttribute(ccmc::strings::variables::y_, ccmc::strings::attributes::actual_min_)).getAttributeFloat();
                max.component3 = (kameleon->getVariableAttribute(ccmc::strings::variables::z_, ccmc::strings::attributes::actual_max_)).getAttributeFloat();

            }
            else
            {
                //					std::cout<<"Retrieving actual_min and actual_max from file \n";
                min.component1 = (kameleon->getVariableAttribute(ccmc::strings::variables::x_, ccmc::strings::attributes::actual_min_)).getAttributeFloat();
                min.component2 = (kameleon->getVariableAttribute(ccmc::strings::variables::y_, ccmc::strings::attributes::actual_min_)).getAttributeFloat();
                min.component3 = (kameleon->getVariableAttribute(ccmc::strings::variables::z_, ccmc::strings::attributes::actual_min_)).getAttributeFloat();

                max.component1 = (kameleon->getVariableAttribute(ccmc::strings::variables::x_, ccmc::strings::attributes::actual_max_)).getAttributeFloat();
                max.component2 = (kameleon->getVariableAttribute(ccmc::strings::variables::y_, ccmc::strings::attributes::actual_max_)).getAttributeFloat();
                max.component3 = (kameleon->getVariableAttribute(ccmc::strings::variables::z_, ccmc::strings::attributes::actual_max_)).getAttributeFloat();
                //					std::cout<<"min"<<min<<" max"<<max<<endl;
            }

        }
    }
    else
    {
        min = minROI;
        max = maxROI;
    }

    /** from Lutz's trace_fieldline_cdf.c **/
    /* calculate maximum iteration step 'dt' to limit spatial advance in
     each direction to delta times the local grid spacing:
     dt*bx<=delta*dComponent1, dt*by<=delta*dComponent2, , dt*bz<=delta*dComponent3 */
    Point3f oldPoint;
    float eps = 1e-5;
    Point3f point;

    float dComponent1 = -0.01;
    float dComponent2 = -0.01;
    float dComponent3 = -0.01;
    Point3f currentVectorData;
    int return_status;
    bool isB1 = false;
    float min_block_size = 1e-2;

    Fieldline fieldline;
    //vector<Point3f> streamline;
    //vector<float> mag1;
    //vector<float> mag2;
    //vector<float> magnitudes;
    //string model_name = gattribute_char_get("model_name");

    float sintilt = std::sin(tilt);
    float costilt = std::cos(tilt);
    float rr;
    float zz1;
    float bdp_rr5;
    float bx_dip;
    float by_dip;
    float bz_dip;
    float bdp;
    string bdp_string;
    string variable_string = variable;

    //vector<Point3f> fieldline1;
    //vector<Point3f> fieldline2;

    // get first half of streamline

    if (model_name == ccmc::strings::models::batsrus_)
    {
#ifdef DEBUG_TRACER
        cerr << "inside. model_name: \"" << model_name << "\"" << endl;
#endif

        if (variable_string == "b1" || variable_string == "b1x" || variable_string == "b1y" || variable_string
            == "b1z")
        {
            isB1 = true;
            std::string bdp_string = (kameleon->getGlobalAttribute("b_dipole")).getAttributeString();
            if (bdp_string != "")
            {
                bdp = boost::lexical_cast<float>(bdp_string);
            }
            else
            {
                bdp = -31100.0;
            }

            tilt = (kameleon->getGlobalAttribute("dipole_tilt")).getAttributeFloat();
#ifdef DEBUG_TRACER
            cerr << "bdp: " << bdp << " tilt: " << tilt << endl;
#endif

        }
    }

    Point3f startPoint(startComponent1, startComponent2, startComponent3);
#ifdef DEBUG_TRACER
    cerr << "before getVector, variable: " << variable << endl;
#endif
    Point3f vectorValue = getVector(variable, startPoint, dComponent1, dComponent2, dComponent3, interpolator);
#ifdef DEBUG_TRACER
    cerr << "After getVector for: " << variable << " = " << vectorValue << endl;
#endif

    if (vectorValue.component1 == missing ||
        vectorValue.component2 == missing ||
        vectorValue.component3 == missing) {
        std::cout << "Fieldline returning empty\n";
        return fieldline; //empty
    }

    //start.addVariableData(variable, vectorValue.magnitude());

    Point3f start = Point3f(startComponent1, startComponent2, startComponent3);
    fieldline.insertPointData(start, vectorValue.magnitude());
    Point3f previous = startPoint;
    bool finished = false;
    int iterations = 0;
    oldPoint.component1 = 1e-20;
    oldPoint.component2 = 1e-20;
    oldPoint.component3 = 1e-20;

#ifdef DEBUG_TRACER
    cerr << "min: " << min << " max: " << max << endl;
    cerr << "finished: " << finished << endl;
#endif

    while (!finished && isValidPoint(previous, min, max))
    {
        Point3f addition;

        if (isB1 == true && bdp != 0.)
        {
            rr = previous.magnitude();
            bdp_rr5 = bdp / pow((double)rr, 5.0);
            zz1 = -1.0 * previous.component1 * sintilt + previous.component3 * costilt;
            bx_dip = bdp_rr5 * (3. * previous.component1 * zz1 + rr * rr * sintilt);
            by_dip = bdp_rr5 * 3. * previous.component2 * zz1;
            bz_dip = bdp_rr5 * (3. * previous.component3 * zz1 - rr * rr * costilt);
            vectorValue.component1 = vectorValue.component1 + bx_dip;
            vectorValue.component2 = vectorValue.component2 + by_dip;
            vectorValue.component3 = vectorValue.component3 + bz_dip;
        }

        float magValue = vectorValue.magnitude();
#ifdef DEBUG_TRACER
        cerr << "magValue: " << magValue << endl;
        cerr << "before ---- eps: " << eps << " dComponent1: " << dComponent1 << " dComponent2: " << dComponent2 << " dComponent3: " << dComponent3 << endl;

#endif
        if (isnan(dComponent1) || dComponent1 < min_block_size)
        {
            dComponent1 = min_block_size;
            dComponent2 = min_block_size;
            dComponent3 = min_block_size;
        }
        if (isnan(dComponent2) || dComponent2 < min_block_size)
        {
            dComponent1 = min_block_size;
            dComponent2 = min_block_size;
            dComponent3 = min_block_size;
        }
        if (isnan(dComponent3) || dComponent3 < min_block_size)
        {
            dComponent1 = min_block_size;
            dComponent2 = min_block_size;
            dComponent3 = min_block_size;
        }
        dComponent1 *= magValue / (fabs(vectorValue.component1) + eps * (fabs(vectorValue.component1) < eps)); // ds = [dx*B/Bx, dy*B/By, dz*B/Bz]
        dComponent2 *= magValue / (fabs(vectorValue.component2) + eps * (fabs(vectorValue.component2) < eps));
        dComponent3 *= magValue / (fabs(vectorValue.component3) + eps * (fabs(vectorValue.component3) < eps));

#ifdef DEBUG_TRACER
        cerr << "after ---- eps: " << eps << " dComponent1: " << dComponent1 << " dComponent2: " << dComponent2 << " dComponent3: " << dComponent3 << endl;
#endif

        // Use Fourth Order Runge-Kutta
        // Should make this method flexible, so user can select order
        if (magValue < eps)
            magValue = eps;

        float dt;

        //		if (dt < .01)
        //			dt = .01;


        Point3f k1 = getVector(variable, previous, dComponent1, dComponent2, dComponent3, interpolator);
        k1.normalize();
        dt = calculateDT(dComponent1, dComponent2, dComponent3, adjusted_dn);
        Point3f k2 = getVector(variable, previous + k1 * (.5 * dt), dComponent1, dComponent2, dComponent3,
            interpolator);
        k2.normalize();
        dt = calculateDT(dComponent1, dComponent2, dComponent3, adjusted_dn);
        Point3f k3 = getVector(variable, previous + k2 * (.5 * dt), dComponent1, dComponent2, dComponent3,
            interpolator);
        k3.normalize();
        dt = calculateDT(dComponent1, dComponent2, dComponent3, adjusted_dn);

#ifdef DEBUG_TRACER
        cerr << "dt: " << dt << endl;
#endif

        Point3f k4 = getVector(variable, previous + k3 * dt, dComponent1, dComponent2, dComponent3, interpolator);
        k4.normalize();
        //addition.component1 = dt/2.0*vectorValue.component1/magValue;
        //addition.component2 = dt/2.0*vectorValue.component2/magValue;
        //addition.component3 = dt/2.0*vectorValue.component3/magValue;
        addition = (k1 + k2 * 2.0f + k3 * 2.0f + k4) * (1.0f / 6.0f);
        //addition.normalize();
        Point3f newPoint = previous + addition * dt;
        float dist = previous.distance(newPoint);

        /** check if tracing went anywhere, else stop **/
        if (dist < 1e-10 || newPoint.distance(oldPoint) < 1e-4)
        {
            finished = true;
            return_status = 3;
        }
        else
        {
            arcLength += dist;
            if (this->useMaxArcLength && arcLength > this->maxArcLength)
            {
                finished = true;
            }
            oldPoint = previous;
            //	 newPointData;
            //	newPointData.setPosition(newPoint);
            iterations++;
            if (iterations > step_max)
                finished = true;

            previous = newPoint;
            vectorValue = getVector(variable, newPoint, dComponent1, dComponent2, dComponent3, interpolator);
            //newPointData.addVariableData(variable, vectorValue.magnitude());
            if (this->isValidPoint(newPoint, min, max))
            {
                newPoint.setCoordinates(Point3f::CARTESIAN);
                fieldline.insertPointData(newPoint, vectorValue.magnitude());
            }
        }

    }

    return fieldline;
}

void FieldlinesState::setExtraQuantityNames(std::vector<std::string> names) {
    _extraQuantityNames = std::move(names);
    _extraQuantities.resize(_extraQuantityNames.size());
}

const std::vector<std::vector<float>>& FieldlinesState::extraQuantities() const {
    return _extraQuantities;
}

const std::vector<std::string>& FieldlinesState::extraQuantityNames() const {
    return _extraQuantityNames;
}

const std::vector<GLsizei>& FieldlinesState::lineCount() const {
    return _lineCount;
}

const std::vector<GLint>& FieldlinesState::lineStart() const {
    return _lineStart;
}

fls::Model FieldlinesState::FieldlinesState::model() const {
    return _model;
}

size_t FieldlinesState::nExtraQuantities() const {
    return _extraQuantities.size();
}

double FieldlinesState::triggerTime() const {
    return _triggerTime;
}

const std::vector<glm::vec3>& FieldlinesState::vertexPositions() const {
    return _vertexPositions;
}

} // namespace openspace
