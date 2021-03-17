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

 #version __CONTEXT__
#define PI 3.1415926538

layout (lines) in;
layout (line_strip, max_vertices = 88) out;

const float EPSILON = 1e-5;
const float RADII = 6378137.0; // Earth is approximated as a sphere 
const float THRESHOLD = -9998;

uniform mat4 modelViewProjection;
uniform float opacity;
uniform vec2 latitudeThreshold;
uniform vec2 longitudeThreshold;
uniform int time;

//in float vs_vertexID[];
in vec4 vs_position[];
in vec4 vs_interpColor[];
in vec2 vs_latlon[];
in float vs_vertexID[];
in ivec2 vs_vertexInfo[];

out vec4 ge_position;
out vec4 ge_interpColor;

float greatCircleDistance(float lat1, float lon1, float lat2, float lon2) {
    // distance between latitudes 
    // and longitudes 
    float dLat = (lat2 - lat1); 
    float dLon = (lon2 - lon1);
  
    // apply formulae 
    float a = pow(sin(dLat / 2.f), 2.f) +  pow(sin(dLon / 2.f), 2.f) *  cos(lat1) * cos(lat2); 
     
    float c = 2 * asin(sqrt(a)); 
 
    return RADII * c; 
}

vec2 findIntermediatePoint(vec2 latlon1, vec2 latlon2, float f) {
    //vec2 latlonR1 = latlon1 * PI / 180.0;
    //vec2 latlonR2 = latlon2 * PI / 180.0;

    float phi1 = latlon1.x; float lambda1 = latlon1.y;
    float phi2 = latlon2.x; float lambda2 = latlon2.y;

    float delta = greatCircleDistance(phi1, lambda1, phi2, lambda2) / RADII; 
    
    float a = (sin(1-f)*delta) / sin(delta);
    float b = sin(f*delta) / sin(delta);

     float x = a*cos(phi1)*cos(lambda1) +  b*cos(phi2)*cos(lambda2);
     float y = a*cos(phi1)*sin(lambda1) +  b*cos(phi2)*sin(lambda2);
     float z = a*sin(phi1) + b*sin(phi2);
     
     float phi3 = atan(z,sqrt(x*x+y*y));
     float lambda3 = atan(y,x);

     return vec2(phi3, lambda3);
}

vec4 geoToCartConversion(float lat, float lon, float alt){

    float x = (RADII + alt) * cos(lat) * cos(lon);
    float y = (RADII + alt)* cos(lat) * sin(lon);
    float z = (RADII + alt)* sin(lat);

    return vec4(x, y, z, 1.0);
}

 void main(){
    
    // Discard erronous positions
    if(length(gl_in[0].gl_Position) < EPSILON || length(gl_in[1].gl_Position) < EPSILON) {
            return;
    }

    float firstSeen = vs_vertexInfo[0].x;
    float lastSeen = vs_vertexInfo[0].y;

    if(firstSeen < float(time) && float(time) < lastSeen) {
        
        // Start point
        ge_position = vs_position[0];
        ge_interpColor = vec4(vec3(vs_interpColor[0]), opacity);
        gl_Position = gl_in[0].gl_Position;
        EmitVertex();

        // Calculate current position
        float t = clamp((float(time) - firstSeen) / (lastSeen - firstSeen), 0.0, 1.0);
        vec2 pointCurrent = findIntermediatePoint(vs_latlon[0], vs_latlon[1], t);
        
        float alt = 10000.0;
        
        // Mid points, start to current
        for(int i = 1; i < 20; ++i) {
            vec2 point = findIntermediatePoint(vs_latlon[0], pointCurrent, float(i)/20.0);
            vec4 position = geoToCartConversion(point.x, point.y, alt);
            ge_position = modelViewProjection * position;
            float midOpacity = 0.2;
            ge_interpColor = vec4(vec3(vs_interpColor[0]), midOpacity*opacity);
            gl_Position = ge_position;
            EmitVertex();
        }

        // End point
        vec4 position = geoToCartConversion(pointCurrent.x, pointCurrent.y, alt);
        ge_position = modelViewProjection * position;
        ge_interpColor = vec4(vec3(vs_interpColor[1]), opacity);
        gl_Position = ge_position;
        EmitVertex();

        EndPrimitive();
    }
    else return;
 }